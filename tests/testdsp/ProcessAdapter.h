#pragma once
#include <vector>
#include <functional>
#include "../../src/dsp/spine/NlSvfCell.h"
#include "../../src/dsp/spine/FilterModel.h"

namespace testdsp {
struct ShaperAdapter {
    std::function<float(float)> fn;
    void prepare(double) {}
    void reset() {}
    void process(float* b, int n) {
        for (int i = 0; i < n; ++i)
            b[i] = fn(b[i]);
    }
};

struct CellAdapter {
    NlSvfCell cell;
    int tap = NlSvfCell::LP;
    double sr = 48000.0;
    float cutoff = 1000.0f, res = 0.0f, resSat = 0.0f;

    void prepare(double s) {
        sr = s;
        cell.prepare(s);
        cell.setCutoff(cutoff);
        cell.setResonance(res);
        cell.setResSat(resSat);
    }

    void reset() {
        cell.reset();
    }

    void process(float* b, int n) {
        for (int i = 0; i < n; ++i) {
            float l = b[i], r = b[i];
            cell.process(l, r, tap);
            b[i] = l;
        }
    }
};

struct ModelAdapter {
    FilterModel* model = nullptr;
    std::unique_ptr<FilterModel::State> state;
    std::vector<float> rscratch;

    void prepare(double s) {
        model->prepare(s);
        state.reset(model->makeState());
        model->reset(*state);
    }

    void reset() {
        model->reset(*state);   // rscratch is overwritten each process() before use; no need to clear
    }

    void process(float* b, int n) {
        // Mono adapter: duplicate L into R-scratch and discard R output.
        if ((int)rscratch.size() < n)
            rscratch.resize((size_t)n);
        std::copy(b, b + n, rscratch.begin());
        model->processStereo(*state, b, rscratch.data(), n);  // L=b, R=scratch (discarded)
    }
};
} // namespace testdsp
