// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QFile>
#include <QString>
#include <rhi/qshader.h>

#include <gtest/gtest.h>

#include <array>

namespace {

QShader load(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

} // namespace

TEST(Shaders, BuildBakesEveryBackendTarget)
{
    const std::array keys{
        QShaderKey(QShader::SpirvShader, QShaderVersion(100)),
        QShaderKey(QShader::GlslShader, QShaderVersion(100, QShaderVersion::GlslEs)),
        QShaderKey(QShader::GlslShader, QShaderVersion(120)),
        QShaderKey(QShader::GlslShader, QShaderVersion(150)),
        QShaderKey(QShader::HlslShader, QShaderVersion(50)),
        QShaderKey(QShader::MslShader, QShaderVersion(12)),
    };
    for (const QString& path :
         {QStringLiteral(":/shaders/probe.vert.qsb"), QStringLiteral(":/shaders/probe.frag.qsb")}) {
        const QShader shader = load(path);
        ASSERT_TRUE(shader.isValid()) << path.toStdString();
        for (const QShaderKey& key : keys) {
            EXPECT_FALSE(shader.shader(key).shader().isEmpty())
                << path.toStdString() << " source " << key.source() << " version " << key.sourceVersion().version();
        }
    }
}
