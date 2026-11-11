// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/exchange.h"

#include "log/log.h"

#include <APIHeaderSection_MakeHeader.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BinTools.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressScope.hxx>
#include <NCollection_Sequence.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Writer.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <cstddef>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace kalve137::geometry {

namespace {

constexpr double kStlDeflection = 0.01;
constexpr double kStlAngle = 0.2;

TopoDS_Compound compound(const std::vector<TopoDS_Shape>& shapes)
{
    BRep_Builder builder;
    TopoDS_Compound result;
    builder.MakeCompound(result);
    for (const TopoDS_Shape& shape : shapes) {
        builder.Add(result, shape);
    }
    return result;
}

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

struct Xcaf {
    Handle(TDocStd_Document) document;
    Handle(XCAFDoc_ShapeTool) shapes;
    Handle(XCAFDoc_ColorTool) colors;
};

Xcaf newXcaf()
{
    Xcaf xcaf;
    xcaf.document = new TDocStd_Document("MDTV-XCAF");
    XCAFApp_Application::GetApplication()->InitDocument(xcaf.document);
    xcaf.shapes = XCAFDoc_DocumentTool::ShapeTool(xcaf.document->Main());
    xcaf.colors = XCAFDoc_DocumentTool::ColorTool(xcaf.document->Main());
    return xcaf;
}

std::string nameOf(const TDF_Label& label)
{
    Handle(TDataStd_Name) name;
    if (!label.FindAttribute(TDataStd_Name::GetID(), name)) {
        return {};
    }
    return TCollection_AsciiString(name->Get()).ToCString();
}

void setName(const TDF_Label& label, const std::string& name)
{
    if (!name.empty()) {
        TDataStd_Name::Set(label, TCollection_ExtendedString(name.c_str(), true));
    }
}

std::optional<Color> colorOf(const Xcaf& xcaf, const TDF_Label& label)
{
    Quantity_Color color;
    if (!xcaf.colors->GetColor(label, XCAFDoc_ColorSurf, color) &&
        !xcaf.colors->GetColor(label, XCAFDoc_ColorGen, color)) {
        return std::nullopt;
    }
    Color result;
    color.Values(result.red, result.green, result.blue, Quantity_TOC_sRGB);
    return result;
}

FaceColors faceColorsOf(const Xcaf& xcaf, const TDF_Label& label, const TopoDS_Shape& solid,
                        const std::optional<Color>& solidColor)
{
    FaceColors colors;
    for (TopExp_Explorer it(solid, TopAbs_FACE); it.More(); it.Next()) {
        TDF_Label faceLabel;
        if (!xcaf.shapes->FindSubShape(label, it.Current(), faceLabel)) {
            continue;
        }
        const std::optional<Color> color = colorOf(xcaf, faceLabel);
        if (color && color != solidColor) {
            colors.set(TopoDS::Face(it.Current()), *color);
        }
    }
    return colors;
}

void collectParts(const Xcaf& xcaf, const TDF_Label& label, const TopLoc_Location& location, const std::string& name,
                  const std::optional<Color>& color, const std::vector<std::string>& path, std::vector<StepPart>& parts)
{
    if (XCAFDoc_ShapeTool::IsAssembly(label)) {
        std::vector<std::string> inner = path;
        inner.push_back(name);
        NCollection_Sequence<TDF_Label> components;
        XCAFDoc_ShapeTool::GetComponents(label, components);
        for (int i = 1; i <= components.Length(); ++i) {
            const TDF_Label& component = components.Value(i);
            TDF_Label referred;
            if (!XCAFDoc_ShapeTool::GetReferredShape(component, referred)) {
                continue;
            }
            std::string partName = nameOf(referred);
            if (partName.empty()) {
                partName = nameOf(component);
            }
            std::optional<Color> partColor = colorOf(xcaf, component);
            if (!partColor) {
                partColor = colorOf(xcaf, referred);
            }
            collectParts(xcaf, referred, location * XCAFDoc_ShapeTool::GetLocation(component),
                         partName.empty() ? name : partName, partColor ? partColor : color, inner, parts);
        }
        return;
    }
    const TopoDS_Shape shape = XCAFDoc_ShapeTool::GetShape(label);
    for (TopExp_Explorer it(shape, TopAbs_SOLID); it.More(); it.Next()) {
        std::optional<Color> solidColor = color;
        TDF_Label solidLabel;
        if (xcaf.shapes->FindSubShape(label, it.Current(), solidLabel)) {
            if (const std::optional<Color> own = colorOf(xcaf, solidLabel)) {
                solidColor = own;
            }
        }
        parts.push_back({name, it.Current().Moved(location), solidColor,
                         faceColorsOf(xcaf, label, it.Current(), solidColor), path});
    }
}

class CallbackProgress : public Message_ProgressIndicator {
public:
    explicit CallbackProgress(const ProgressCallback& callback) : m_callback(callback) {}

    bool UserBreak() override
    {
        if (!m_stopped && !m_callback(GetPosition())) {
            m_stopped = true;
        }
        return m_stopped;
    }

    void Show(const Message_ProgressScope&, const bool) override { UserBreak(); }

private:
    const ProgressCallback& m_callback;
    bool m_stopped = false;
};

} // namespace

void writeStep(const std::vector<StepPart>& parts, const std::filesystem::path& path, const std::string& application)
{
    if (parts.empty()) {
        throw std::runtime_error("Nothing to export");
    }
    const Xcaf xcaf = newXcaf();
    std::map<std::vector<std::string>, TDF_Label> assemblies;
    const auto assemblyAt = [&](const std::vector<std::string>& path) {
        TDF_Label parent;
        for (std::size_t depth = 1; depth <= path.size(); ++depth) {
            const std::vector<std::string> prefix(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(depth));
            auto [it, added] = assemblies.try_emplace(prefix);
            if (added) {
                it->second = xcaf.shapes->NewShape();
                setName(it->second, prefix.back());
                if (!parent.IsNull()) {
                    xcaf.shapes->AddComponent(parent, it->second, TopLoc_Location());
                }
            }
            parent = it->second;
        }
        return parent;
    };
    for (const StepPart& part : parts) {
        const TDF_Label assembly = assemblyAt(part.path);
        const TopoDS_Shape shape = assembly.IsNull() ? part.shape : part.shape.Located(TopLoc_Location());
        const TDF_Label label = xcaf.shapes->AddShape(shape, false);
        setName(label, part.name);
        if (part.color) {
            xcaf.colors->SetColor(
                label, Quantity_Color(part.color->red, part.color->green, part.color->blue, Quantity_TOC_sRGB),
                XCAFDoc_ColorGen);
        }
        for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
            const std::optional<Color> color = part.faceColors.find(TopoDS::Face(it.Current()));
            if (!color) {
                continue;
            }
            const TDF_Label face = xcaf.shapes->AddSubShape(label, it.Current());
            if (!face.IsNull()) {
                xcaf.colors->SetColor(face, Quantity_Color(color->red, color->green, color->blue, Quantity_TOC_sRGB),
                                      XCAFDoc_ColorSurf);
            }
        }
        if (!assembly.IsNull()) {
            xcaf.shapes->AddComponent(assembly, label, part.shape.Location());
        }
    }
    xcaf.shapes->UpdateAssemblies();
    STEPCAFControl_Writer writer;
    writer.SetNameMode(true);
    writer.SetColorMode(true);
    if (!writer.Transfer(xcaf.document, STEPControl_AsIs)) {
        throw std::runtime_error("STEP translation failed");
    }
    APIHeaderSection_MakeHeader header(writer.ChangeWriter().Model());
    header.SetName(new TCollection_HAsciiString(utf8(path.filename()).c_str()));
    header.SetOriginatingSystem(new TCollection_HAsciiString(application.c_str()));
    if (writer.Write(utf8(path).c_str()) != IFSelect_RetDone) {
        throw std::runtime_error("Could not write " + utf8(path));
    }
    K137_INFO(Io, "Exported {} bodies as STEP to {}", parts.size(), utf8(path));
}

void writeStl(const std::vector<TopoDS_Shape>& shapes, const std::filesystem::path& path)
{
    if (shapes.empty()) {
        throw std::runtime_error("Nothing to export");
    }
    const TopoDS_Compound all = compound(shapes);
    BRepMesh_IncrementalMesh mesh(all, kStlDeflection, false, kStlAngle, true);
    StlAPI_Writer writer;
    writer.ASCIIMode() = false;
    if (!writer.Write(all, utf8(path).c_str())) {
        throw std::runtime_error("Could not write " + utf8(path));
    }
    K137_INFO(Io, "Exported {} shapes as STL to {}", shapes.size(), utf8(path));
}

std::vector<StepPart> readStepParts(const std::filesystem::path& path, const ProgressCallback& progress)
{
    const ProgressCallback report = progress ? progress : [](double) { return true; };
    STEPCAFControl_Reader reader;
    reader.SetNameMode(true);
    reader.SetColorMode(true);
    if (reader.ReadFile(utf8(path).c_str()) != IFSelect_RetDone) {
        throw std::runtime_error("Could not read " + utf8(path) + " as STEP");
    }
    if (!report(0.0)) {
        throw ImportCanceled();
    }
    const Xcaf xcaf = newXcaf();
    const Handle(CallbackProgress) indicator = new CallbackProgress(report);
    const bool transferred = reader.Transfer(xcaf.document, indicator->Start());
    if (indicator->UserBreak()) {
        throw ImportCanceled();
    }
    if (!transferred) {
        throw std::runtime_error("STEP translation failed for " + utf8(path));
    }
    NCollection_Sequence<TDF_Label> roots;
    xcaf.shapes->GetFreeShapes(roots);
    std::vector<StepPart> parts;
    for (int i = 1; i <= roots.Length(); ++i) {
        const TDF_Label& root = roots.Value(i);
        collectParts(xcaf, root, TopLoc_Location(), nameOf(root), colorOf(xcaf, root), {}, parts);
    }
    if (parts.empty()) {
        throw std::runtime_error("The STEP file holds no solids");
    }
    if (!report(1.0)) {
        throw ImportCanceled();
    }
    K137_INFO(Io, "Read {} solids from STEP {}", parts.size(), utf8(path));
    return parts;
}

std::string toBrep(const TopoDS_Shape& shape)
{
    std::ostringstream out;
    BRepTools::Write(shape, out);
    return out.str();
}

TopoDS_Shape fromBrep(const std::string& text)
{
    std::istringstream in(text);
    TopoDS_Shape shape;
    BRep_Builder builder;
    try {
        BRepTools::Read(shape, in, builder);
    } catch (const Standard_Failure&) {
        shape.Nullify();
    }
    if (shape.IsNull()) {
        throw std::runtime_error("The stored shape is damaged");
    }
    return shape;
}

std::string toBinaryBrep(const TopoDS_Shape& shape)
{
    std::ostringstream out(std::ios::binary);
    BinTools::Write(shape, out, false, false, BinTools_FormatVersion_CURRENT);
    return out.str();
}

TopoDS_Shape fromBinaryBrep(const std::string& bytes)
{
    std::istringstream in(bytes, std::ios::binary);
    TopoDS_Shape shape;
    try {
        BinTools::Read(shape, in);
    } catch (const Standard_Failure&) {
        shape.Nullify();
    }
    if (shape.IsNull()) {
        throw std::runtime_error("The stored shape is damaged");
    }
    return shape;
}

} // namespace kalve137::geometry
