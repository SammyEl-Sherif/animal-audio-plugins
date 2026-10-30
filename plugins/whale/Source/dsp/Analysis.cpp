#include "Analysis.h"

#include <animalaudio/dsp/ChangePoints.h>
#include <animalaudio/dsp/Notes.h>
#include <animalaudio/dsp/ProbabilisticYin.h>
#include <animalaudio/dsp/Psola.h>
#include <animalaudio/dsp/Yin.h>

#include <algorithm>
#include <cmath>

namespace whale::dsp
{
namespace
{
namespace aa = animalaudio::dsp;

constexpr int kShortestVoicedFrames = 4;
constexpr int kLongestBridgeFrames = 10;
constexpr float kBridgeMaxZeroCrossingsPerSecond = 3000.0f;
constexpr double kDriftCutoffHz = 4.0;
constexpr double kSegmentPenalty = 2.5;
constexpr int kShortestNoteFrames = 9;
constexpr float kMergeBelowSemitones = 0.6f;
constexpr float kSyllableDipDb = 6.0f;
constexpr double kLeadInSeconds = 0.2;
constexpr double kTailSeconds = 0.15;
constexpr double kPhrasePauseSeconds = 0.2;
constexpr double kShortestCutGapSeconds = 0.03;

struct Run
{
    int first = 0;
    int last = 0; // inclusive
};

std::vector<Run> voicedRuns(const std::vector<AnalysisFrame>& frames)
{
    std::vector<Run> runs;
    const int n = static_cast<int>(frames.size());
    for (int i = 0; i < n;)
    {
        if (frames[static_cast<std::size_t>(i)].midi < 0.0f)
        {
            ++i;
            continue;
        }
        int j = i;
        while (j + 1 < n && frames[static_cast<std::size_t>(j + 1)].midi >= 0.0f)
            ++j;
        runs.push_back({i, j});
        i = j + 1;
    }
    return runs;
}

std::vector<float> zeroCrossingRates(const float* x, std::int64_t length, double rate, int hop, std::size_t count)
{
    std::vector<float> zcr(count, 0.0f);
    const auto half = static_cast<std::int64_t>(rate * 0.004);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::int64_t centre = static_cast<std::int64_t>(i) * hop;
        const std::int64_t a = std::max<std::int64_t>(1, centre - half);
        const std::int64_t b = std::min<std::int64_t>(length, centre + half);
        int crossings = 0;
        for (std::int64_t k = a; k < b; ++k)
            crossings += (x[k - 1] < 0.0f) != (x[k] < 0.0f) ? 1 : 0;
        zcr[i] = b > a ? static_cast<float>(crossings * rate / static_cast<double>(b - a)) : 0.0f;
    }
    return zcr;
}

void dropShortVoicedRuns(std::vector<AnalysisFrame>& frames)
{
    for (const auto& run : voicedRuns(frames))
        if (run.last - run.first + 1 < kShortestVoicedFrames)
            for (int i = run.first; i <= run.last; ++i)
                frames[static_cast<std::size_t>(i)].midi = -1.0f;
}

void bridgeDropouts(std::vector<AnalysisFrame>& frames, const std::vector<float>& zcr, float floorDb)
{
    const auto runs = voicedRuns(frames);
    for (std::size_t r = 1; r < runs.size(); ++r)
    {
        const int before = runs[r - 1].last;
        const int after = runs[r].first;
        const int gap = after - before - 1;
        if (gap > kLongestBridgeFrames)
            continue;
        const auto& a = frames[static_cast<std::size_t>(before)];
        const auto& b = frames[static_cast<std::size_t>(after)];
        if (std::abs(a.midi - b.midi) > 14.0f)
            continue;
        const float needDb = std::max(std::min(a.levelDb, b.levelDb) - 15.0f, floorDb + 6.0f);
        bool loudAndTonal = true;
        for (int i = before + 1; i < after; ++i)
            loudAndTonal = loudAndTonal && frames[static_cast<std::size_t>(i)].levelDb >= needDb &&
                           zcr[static_cast<std::size_t>(i)] < kBridgeMaxZeroCrossingsPerSecond;
        if (!loudAndTonal)
            continue;
        for (int i = before + 1; i < after; ++i)
        {
            const float t = static_cast<float>(i - before) / static_cast<float>(gap + 1);
            frames[static_cast<std::size_t>(i)].midi = a.midi + (b.midi - a.midi) * t;
        }
    }
}

void computeDrift(std::vector<AnalysisFrame>& frames, double frameRate)
{
    const auto runs = voicedRuns(frames);
    std::vector<double> values;
    for (const auto& run : runs)
    {
        values.clear();
        for (int i = run.first; i <= run.last; ++i)
            values.push_back(frames[static_cast<std::size_t>(i)].midi);
        const auto filter = [&](auto begin, auto end)
        {
            auto biquad = aa::yin::Biquad::lowpass(frameRate, kDriftCutoffHz, 0.70710678);
            const double x0 = *begin;
            biquad.z1 = x0 * (1.0 - biquad.b0);
            biquad.z2 = x0 * (biquad.b2 - biquad.a2);
            for (auto it = begin; it != end; ++it)
                *it = biquad.process(*it);
        };
        filter(values.begin(), values.end());
        filter(values.rbegin(), values.rend());
        for (int i = run.first; i <= run.last; ++i)
            frames[static_cast<std::size_t>(i)].drift =
                static_cast<float>(values[static_cast<std::size_t>(i - run.first)]);
    }
}

/** Frames inside [first, last] where a level dip separates two re-sung syllables. */
std::vector<int> syllableDips(const std::vector<AnalysisFrame>& frames, int first, int last, double frameRate)
{
    std::vector<int> dips;
    const int reach = std::max(2, static_cast<int>(std::lround(0.06 * frameRate)));
    const int margin = std::max(kShortestNoteFrames, static_cast<int>(std::lround(0.04 * frameRate)));
    for (int i = first + margin; i <= last - margin; ++i)
    {
        const float level = frames[static_cast<std::size_t>(i)].levelDb;
        bool minimum = true;
        float leftPeak = level, rightPeak = level;
        for (int k = 1; k <= reach; ++k)
        {
            const float l = frames[static_cast<std::size_t>(std::max(first, i - k))].levelDb;
            const float r = frames[static_cast<std::size_t>(std::min(last, i + k))].levelDb;
            minimum = minimum && l >= level && r >= level;
            leftPeak = std::max(leftPeak, l);
            rightPeak = std::max(rightPeak, r);
        }
        if (minimum && leftPeak - level >= kSyllableDipDb && rightPeak - level >= kSyllableDipDb &&
            (dips.empty() || i - dips.back() >= kShortestNoteFrames))
            dips.push_back(i);
    }
    return dips;
}

/** Note starts (frame indices) inside one voiced stretch [first, last], first included. */
std::vector<int> splitStretch(const TakeAnalysis& analysis, int first, int last, double frameRate)
{
    const auto& frames = analysis.frames;
    std::vector<int> starts{first};
    std::vector<int> pieces{first};
    for (const int dip : syllableDips(frames, first, last, frameRate))
        pieces.push_back(dip);
    pieces.push_back(last + 1);

    for (std::size_t p = 0; p + 1 < pieces.size(); ++p)
    {
        const int a = pieces[p];
        const int b = pieces[p + 1]; // exclusive
        if (p > 0)
            starts.push_back(a);
        std::vector<float> values, weights;
        for (int i = a; i < b; ++i)
        {
            const auto& f = frames[static_cast<std::size_t>(i)];
            const float before = frames[static_cast<std::size_t>(std::max(a, i - 1))].midi;
            const float after = frames[static_cast<std::size_t>(std::min(b - 1, i + 1))].midi;
            const float slope = 0.5f * std::abs(after - before) / 0.15f;
            values.push_back(f.drift);
            weights.push_back(1.0f / (1.0f + slope * slope));
        }
        const auto segments = aa::segmentConstantMean(values, weights, kSegmentPenalty, kShortestNoteFrames);
        for (std::size_t s = 1; s < segments.size(); ++s)
        {
            // Move the boundary to the steepest point of the sung pitch nearby.
            const int c = a + segments[s];
            int best = c;
            float steepest = -1.0f;
            for (int i = std::max(a + 2, c - 6); i <= std::min(b - 3, c + 6); ++i)
            {
                const float slope = std::abs(frames[static_cast<std::size_t>(i + 1)].midi -
                                             frames[static_cast<std::size_t>(i - 1)].midi);
                if (slope > steepest)
                {
                    steepest = slope;
                    best = i;
                }
            }
            starts.push_back(best);
        }
    }
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

    // Two boundaries inside one slide: keep the steeper (piece starts always stay).
    const auto slopeAt = [&](int i)
    {
        const int before = std::max(first, i - 1);
        const int after = std::min(last, i + 1);
        return std::abs(frames[static_cast<std::size_t>(after)].midi - frames[static_cast<std::size_t>(before)].midi);
    };
    const auto isPiece = [&](int frame) { return std::find(pieces.begin(), pieces.end(), frame) != pieces.end(); };
    for (std::size_t k = 1; k < starts.size();)
    {
        const bool tooClose = starts[k] - starts[k - 1] < kShortestNoteFrames;
        const bool tooCloseToEnd = k + 1 == starts.size() && last + 1 - starts[k] < kShortestNoteFrames;
        if (!tooClose && !tooCloseToEnd)
        {
            ++k;
            continue;
        }
        std::size_t drop = k;
        if (tooClose && !isPiece(starts[k]) && !isPiece(starts[k - 1]))
            drop = slopeAt(starts[k]) > slopeAt(starts[k - 1]) ? k - 1 : k;
        else if (tooClose && isPiece(starts[k]))
            drop = k - 1;
        if (isPiece(starts[drop]))
        {
            ++k;
            continue;
        }
        starts.erase(starts.begin() + static_cast<std::ptrdiff_t>(drop));
        k = std::max<std::size_t>(1, drop);
    }

    // Merge neighbours that are really the same pitch (Tony's 2/3-semitone rule, a little tighter), unless a level
    // dip separated them.
    const auto& isDip = isPiece;
    const auto centre = [&](std::size_t k)
    {
        const int from = starts[k];
        const int to = k + 1 < starts.size() ? starts[k + 1] - 1 : last;
        return analysis.centreBetween(static_cast<double>(from) * analysis.hop, static_cast<double>(to) * analysis.hop);
    };
    for (bool merged = true; merged && starts.size() > 1;)
    {
        merged = false;
        float smallest = kMergeBelowSemitones;
        std::size_t at = 0;
        for (std::size_t k = 1; k < starts.size(); ++k)
        {
            if (isDip(starts[k]))
                continue;
            const float difference = std::abs(centre(k) - centre(k - 1));
            if (difference < smallest)
            {
                smallest = difference;
                at = k;
            }
        }
        if (at > 0)
        {
            starts.erase(starts.begin() + static_cast<std::ptrdiff_t>(at));
            merged = true;
        }
    }
    return starts;
}

void placeMarks(const float* mono, std::int64_t length, TakeAnalysis& analysis, const std::vector<Run>& runs)
{
    const auto at = [mono, length](std::int64_t i) { return i >= 0 && i < length ? mono[i] : 0.0f; };
    const auto periodAt = [&](double sample)
    {
        const float midi = analysis.midiAt(sample);
        return static_cast<float>(analysis.rate / aa::midiToHz(std::max(midi, 20.0f)));
    };
    for (std::size_t r = 0; r < runs.size(); ++r)
    {
        const double begin = std::max(0.0, (runs[r].first - 0.5) * analysis.hop);
        const double end = std::min(static_cast<double>(length), (runs[r].last + 0.5) * analysis.hop);
        float period = periodAt(begin + 0.5 * analysis.hop);
        double mark = aa::psola::strongestNear(at, begin + 0.5 * period, period, begin);
        const auto firstIndex = analysis.marks.size();
        while (mark < end)
        {
            analysis.marks.push_back({mark, period, static_cast<int>(r)});
            period = periodAt(mark);
            const double next = aa::psola::alignToPrevious(at, mark, mark + period, period);
            if (next <= mark + 0.5 * period)
                break;
            mark = next;
        }
        for (auto i = firstIndex; i + 1 < analysis.marks.size(); ++i)
            analysis.marks[i].period = static_cast<float>(analysis.marks[i + 1].position - analysis.marks[i].position);
    }
}
} // namespace

bool analyseTake(const float* mono, std::int64_t length, double rate, TakeAnalysis& out,
                 const std::function<bool(float)>& progress)
{
    out = TakeAnalysis{};
    out.rate = rate;
    if (length <= 0 || rate <= 0.0)
        return true;

    aa::ProbabilisticYin::Settings settings;
    settings.gateDb = -70.0f;
    const auto track = aa::ProbabilisticYin::analyse(mono, length, rate, settings,
                                                     [&](float p) { return !progress || progress(0.75f * p); });
    if (track.frames.empty())
    {
        out = TakeAnalysis{};
        return false;
    }

    out.hop = track.hop;
    out.length = length;
    const double frameRate = rate / track.hop;
    float peakDb = -120.0f;
    for (const auto& f : track.frames)
    {
        AnalysisFrame frame;
        frame.midi = f.hz > 0.0f ? aa::hzToMidi(f.hz) : -1.0f;
        frame.levelDb = f.rmsDb;
        frame.voicing = f.voicing;
        out.frames.push_back(frame);
        peakDb = std::max(peakDb, f.rmsDb);
    }
    out.floorDb = std::max(peakDb - 45.0f, -70.0f);

    const auto zcr = zeroCrossingRates(mono, length, rate, out.hop, out.frames.size());
    dropShortVoicedRuns(out.frames);
    bridgeDropouts(out.frames, zcr, out.floorDb);
    dropShortVoicedRuns(out.frames);
    computeDrift(out.frames, frameRate);

    const auto runs = voicedRuns(out.frames);
    const auto frameStart = [&](int frame)
    { return std::clamp((frame - 0.5) * out.hop, 0.0, static_cast<double>(length)); };

    // Notes inside each word.
    for (std::size_t r = 0; r < runs.size(); ++r)
    {
        const auto starts = splitStretch(out, runs[r].first, runs[r].last, frameRate);
        for (std::size_t k = 0; k < starts.size(); ++k)
        {
            DetectedNote note;
            note.voicedStart = note.start = frameStart(starts[k]);
            note.voicedEnd = note.end =
                k + 1 < starts.size() ? frameStart(starts[k + 1]) : frameStart(runs[r].last + 1);
            note.word = static_cast<int>(r);
            note.linkedToNext = k + 1 < starts.size();
            note.centre = out.centreBetween(note.voicedStart, note.voicedEnd);
            out.notes.push_back(note);
        }
    }

    // Consonants, phrases and render cuts in the gaps between words.
    const float activeDb = out.floorDb + 10.0f;
    const auto active = [&](int frame) { return out.frames[static_cast<std::size_t>(frame)].levelDb > activeDb; };
    const int frameCount = static_cast<int>(out.frames.size());
    const int leadFrames = static_cast<int>(kLeadInSeconds * frameRate);
    const int tailFrames = static_cast<int>(kTailSeconds * frameRate);
    out.cuts.push_back(0.0);
    int phrase = 0;
    std::size_t noteIndex = 0;
    for (std::size_t r = 0; r <= runs.size(); ++r)
    {
        const int gapFirst = r == 0 ? 0 : runs[r - 1].last + 1;
        const int gapLast = r == runs.size() ? frameCount - 1 : runs[r].first - 1; // inclusive
        // The middle of the quietest stretch (within 1 dB of the minimum) of the pause.
        int quietest = r == 0 ? -1 : (r == runs.size() ? frameCount : gapFirst);
        if (r > 0 && r < runs.size())
        {
            float lowest = 1.0e9f;
            for (int i = gapFirst; i <= gapLast; ++i)
                lowest = std::min(lowest, out.frames[static_cast<std::size_t>(i)].levelDb);
            int bestFirst = gapFirst, bestLength = 0;
            for (int i = gapFirst; i <= gapLast;)
            {
                int j = i;
                while (j <= gapLast && out.frames[static_cast<std::size_t>(j)].levelDb <= lowest + 1.0f)
                    ++j;
                if (j - i > bestLength)
                {
                    bestFirst = i;
                    bestLength = j - i;
                }
                i = std::max(i + 1, j);
            }
            quietest = bestFirst + bestLength / 2;
        }

        double tailEnd = 0.0;
        if (r > 0)
        {
            int end = gapFirst;
            while (end <= gapLast && end < quietest && end - gapFirst < tailFrames && active(end))
                ++end;
            tailEnd = frameStart(end);
            out.notes[noteIndex - 1].end = std::max(out.notes[noteIndex - 1].end, tailEnd);
        }
        if (r < runs.size())
        {
            int begin = gapLast + 1;
            while (begin - 1 >= gapFirst && begin - 1 > quietest && gapLast + 1 - begin < leadFrames &&
                   active(begin - 1))
                --begin;
            const double leadStart = std::max(frameStart(begin), tailEnd);
            auto& first = out.notes[noteIndex];
            first.start = std::min(first.start, leadStart);
            if (r > 0)
            {
                const auto& previous = out.notes[noteIndex - 1];
                if ((first.start - previous.end) / rate >= kPhrasePauseSeconds)
                    ++phrase;
                if ((runs[r].first - runs[r - 1].last) / frameRate >= kShortestCutGapSeconds)
                    out.cuts.push_back(std::clamp(static_cast<double>(quietest) * out.hop, previous.end, first.start));
            }
            for (; noteIndex < out.notes.size() && out.notes[noteIndex].word == static_cast<int>(r); ++noteIndex)
                out.notes[noteIndex].phrase = phrase;
        }
    }
    out.cuts.push_back(static_cast<double>(length));

    if (progress && !progress(0.8f))
    {
        out = TakeAnalysis{};
        return false;
    }
    placeMarks(mono, length, out, runs);
    if (progress && !progress(1.0f))
    {
        out = TakeAnalysis{};
        return false;
    }
    return true;
}
} // namespace whale::dsp
