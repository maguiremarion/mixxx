#include "waveform/renderers/allshader/waveformrenderbeat.h"

#include <QDomNode>
#include <algorithm>

#include "engine/engine.h"
#include "moc_waveformrenderbeat.cpp"
#include "rendergraph/geometry.h"
#include "rendergraph/material/unicolormaterial.h"
#include "rendergraph/vertexupdaters/vertexupdater.h"
#include "skin/legacy/skincontext.h"
#include "track/track.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "waveform/waveform.h"
#include "waveform/waveformwidgetfactory.h"
#include "widget/wskincolor.h"

using namespace rendergraph;

namespace allshader {

WaveformRenderBeat::WaveformRenderBeat(WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : ::WaveformRendererAbstract(waveformWidget),
          m_pDownbeatNode(nullptr),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip) {
    initForRectangles<UniColorMaterial>(0);
    setUsePreprocess(true);

    auto pNode = std::make_unique<rendergraph::GeometryNode>();
    m_pDownbeatNode = pNode.get();
    m_pDownbeatNode->initForRectangles<UniColorMaterial>(0);
    appendChildNode(std::move(pNode));
}

void WaveformRenderBeat::setup(const QDomNode& node, const SkinContext& skinContext) {
    m_color = QColor(skinContext.selectString(node, QStringLiteral("BeatColor")));
    m_color = WSkinColor::getCorrectColor(m_color).toRgb();
}

void WaveformRenderBeat::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(painter);
    Q_UNUSED(event);
    DEBUG_ASSERT(false);
}

void WaveformRenderBeat::preprocess() {
    if (!preprocessInner()) {
        geometry().allocate(0);
        markDirtyGeometry();
        m_pDownbeatNode->geometry().allocate(0);
        m_pDownbeatNode->markDirtyGeometry();
    }
}

bool WaveformRenderBeat::preprocessInner() {
    const TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();

    if (!trackInfo || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return false;
    }

    const bool isStemTrack = trackInfo && trackInfo->hasStem() &&
            trackInfo->getWaveform() && trackInfo->getWaveform()->hasStem();
    const bool splitStemTracks = isStemTrack && WaveformWidgetFactory::isCreated() &&
            WaveformWidgetFactory::instance()->isStemSplitTracks();

    auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                         : ::WaveformRendererAbstract::Play;

    mixxx::BeatsPointer trackBeats = trackInfo->getBeats();
    if (!trackBeats) {
        return false;
    }

#ifndef __SCENEGRAPH__
    int alpha = m_waveformRenderer->getBeatGridAlpha();
    if (alpha == 0) {
        return false;
    }
    m_color.setAlphaF(alpha / 100.0f);
#endif

    if (!m_color.alpha()) {
        // Don't render the beatgrid lines is there are fully transparent
        return false;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0.0) {
        return false;
    }

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition(positionType);
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition(positionType);

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);

    if (!startPosition.isValid() || !endPosition.isValid()) {
        return false;
    }

    const float rendererBreadth = m_waveformRenderer->getBreadth();

    const int numVerticesPerLine = 6; // 2 triangles

    // Count the number of beats in the range to reserve space in the m_vertices vector.
    // Note that we could also use
    //   int numBearsInRange = trackBeats->numBeatsInRange(startPosition, endPosition);
    // for this, but there have been reports of that method failing with a DEBUG_ASSERT.
    int numBeatsInRange = 0;
    // Index of the first displayed beat in the whole grid, to tell which beats start a bar.
    const auto firstShownBeat = trackBeats->iteratorFrom(startPosition);
    const int firstShownIndex = static_cast<int>(firstShownBeat - trackBeats->cbegin());
    const int downbeatPhase = trackInfo->downbeatPhase();
    const auto isDownbeat = [downbeatPhase](int beatIndex) {
        return ((beatIndex - downbeatPhase) % 4 + 4) % 4 == 0;
    };
    // Bar starts are drawn on top in red, except in the slip and per-stem layouts.
    const bool drawDownbeats = !m_isSlipRenderer && !splitStemTracks;
    int numDownbeatsInRange = 0;
    for (auto it = firstShownBeat; it != trackBeats->cend() && *it <= endPosition; ++it) {
        if (drawDownbeats && isDownbeat(firstShownIndex + numBeatsInRange)) {
            numDownbeatsInRange++;
        }
        numBeatsInRange++;
    }

    // Beats are short ticks at the top and bottom edge, not lines through the whole waveform
    // (the slip renderer keeps one tick, at the top).
    const int numBoxesPerBeat = (m_isSlipRenderer && splitStemTracks)
            ? mixxx::kMaxSupportedStems
            : (m_isSlipRenderer ? 1 : 2);
    const int reserved = numBeatsInRange * numVerticesPerLine * numBoxesPerBeat;
    geometry().allocate(reserved);

    VertexUpdater vertexUpdater{geometry().vertexDataAs<Geometry::Point2D>()};

    m_pDownbeatNode->geometry().allocate(numDownbeatsInRange * numVerticesPerLine * 2);
    VertexUpdater downbeatUpdater{
            m_pDownbeatNode->geometry().vertexDataAs<Geometry::Point2D>()};

    const float boxBreadth = splitStemTracks
            ? rendererBreadth / static_cast<float>(mixxx::kMaxSupportedStems)
            : rendererBreadth;

    const float tickLength = std::min(rendererBreadth / 4.f, 14.f);
    const float downbeatTickLength = std::min(rendererBreadth / 3.f, 22.f);

    int beatIndex = firstShownIndex;
    for (auto it = trackBeats->iteratorFrom(startPosition);
            it != trackBeats->cend() && *it <= endPosition;
            ++it, ++beatIndex) {
        double beatPosition = it->toEngineSamplePos();
        double xBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(
                        beatPosition, positionType);

        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;

        const float x1 = static_cast<float>(xBeatPoint);
        const float x2 = x1 + 2.f;

        if (m_isSlipRenderer && splitStemTracks) {
            for (int stemIdx = 0; stemIdx < mixxx::kMaxSupportedStems; ++stemIdx) {
                const float posy1 = stemIdx * boxBreadth;
                const float posy2 = posy1 + boxBreadth / 2.f;
                vertexUpdater.addRectangle({x1, posy1}, {x2, posy2});
            }
        } else {
            vertexUpdater.addRectangle({x1, 0.f}, {x2, tickLength});
            if (!m_isSlipRenderer) {
                vertexUpdater.addRectangle({x1, rendererBreadth - tickLength}, {x2, rendererBreadth});
            }
            if (drawDownbeats && isDownbeat(beatIndex)) {
                // the first beat of a bar: a longer tick, in red, as wide as the others
                downbeatUpdater.addRectangle({x1, 0.f}, {x2, downbeatTickLength});
                downbeatUpdater.addRectangle({x1, rendererBreadth - downbeatTickLength},
                        {x2, rendererBreadth});
            }
        }
    }
    markDirtyGeometry();
    m_pDownbeatNode->markDirtyGeometry();

    DEBUG_ASSERT(reserved == vertexUpdater.index());

    material().setUniform(1, m_color);
    markDirtyMaterial();
    // Rekordbox-style red for the first beat of each bar
    m_pDownbeatNode->material().setUniform(1, QColor(0xff, 0x3b, 0x3b));
    m_pDownbeatNode->markDirtyMaterial();

    return true;
}

} // namespace allshader
