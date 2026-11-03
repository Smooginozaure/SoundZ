// Transposition par demi-tons : étirement temporel (signalsmith-stretch) puis relecture à vitesse
// 2^(demi-tons/12) (Resampler). Hauteur exacte à moins de 0,2 centième, niveau inchangé.
#pragma once

#include <cmath>
#include <vector>

#include "resampler.h"
#include "signalsmith-stretch/signalsmith-stretch.h"

template <int Channels>
class Transposer {
public:
    static constexpr int kMinSemitones = -4;
    static constexpr int kMaxSemitones = 4;
    static constexpr int kBlockSamples = 2880;    // 60 ms à 48 kHz
    static constexpr int kIntervalSamples = 720;  // 15 ms

    void configure(int maxOutputFrames) {
        stretch_.configure(Channels, kBlockSamples, kIntervalSamples);
        std::vector<double> ratios;
        for (int st = kMinSemitones; st <= kMaxSemitones; ++st) ratios.push_back(std::pow(2.0, st / 12.0));
        resampler_.prepare(ratios, maxOutputFrames);
        setSemitones(0);
    }

    void setSemitones(int semitones) { resampler_.select(size_t(semitones - kMinSemitones)); }

    // `inputFrames` peut différer légèrement de `outputFrames` (rattrapage des horloges).
    template <class Inputs>
    void process(Inputs&& inputs, int inputFrames, float* const* outputs, int outputFrames) {
        int stretched = resampler_.needed(outputFrames);
        float* targets[Channels];
        for (int c = 0; c < Channels; ++c) targets[c] = resampler_.writePointer(c);
        stretch_.process(inputs, inputFrames, targets, stretched);
        resampler_.commit(stretched);
        resampler_.process(outputs, outputFrames);
    }

    int latencyFrames() const {
        return stretch_.inputLatency() + stretch_.outputLatency() + Resampler<Channels>::kHalf;
    }

private:
    signalsmith::stretch::SignalsmithStretch<float> stretch_;
    Resampler<Channels> resampler_;
};
