// Branding raster generator: render an SVG to crisp square PNGs at the requested sizes,
// using Qt's SVG support (QIcon -> the svg iconengine plugin, which rasterises the vector
// natively at each size). Only needs Qt6::Gui -- no Qt6Svg dev package. ImageMagick can't
// rasterise our logo SVG, so this is the source of all branding PNGs (then `convert`
// assembles the .ico from them). Driven by assets/branding/regen_icons.sh.
//
//   svg2png <in.svg> <out_dir> <prefix> [size ...]   (default sizes 16 24 32 48 64 128 256 512)
#include <cstdio>
#include <cstdlib>

#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QPixmap>
#include <QString>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc < 4) {
        std::fprintf(stderr, "usage: svg2png <in.svg> <out_dir> <prefix> [size ...]\n");
        return 2;
    }
    const QString svg = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = QString::fromLocal8Bit(argv[2]);
    const QString prefix = QString::fromLocal8Bit(argv[3]);
    QDir().mkpath(out_dir);

    QIcon icon(svg); // scalable SVG icons report no availableSizes(); validate by rendering
    if (icon.pixmap(64, 64).isNull()) {
        std::fprintf(stderr, "svg2png: could not render SVG '%s' (is the Qt svg plugin present?)\n",
                     argv[1]);
        return 1;
    }

    QList<int> sizes;
    for (int i = 4; i < argc; ++i) {
        sizes << std::atoi(argv[i]);
    }
    if (sizes.isEmpty()) {
        sizes = {16, 24, 32, 48, 64, 128, 256, 512};
    }

    for (int s : sizes) {
        const QPixmap pix = icon.pixmap(s, s); // vector rendered natively at s x s
        const QString path = QStringLiteral("%1/%2%3.png").arg(out_dir, prefix).arg(s);
        if (pix.isNull() || !pix.save(path, "PNG")) {
            std::fprintf(stderr, "svg2png: failed to write %s\n", path.toLocal8Bit().constData());
            return 1;
        }
        std::printf("svg2png: %s (%dx%d)\n", path.toLocal8Bit().constData(), pix.width(),
                    pix.height());
    }
    return 0;
}
