#pragma once
#include <cmath>
#include <cstdint>
namespace axrb::protocol {
struct LayerColor {
    float scale[4]{1,1,1,1};
    float bias[4]{};
    bool identity() const {
        for (int i=0;i<4;++i) if (scale[i]!=1 || bias[i]!=0) return false;
        return true;
    }
    bool valid() const {
        for (int i=0;i<4;++i) if (!std::isfinite(scale[i]) || !std::isfinite(bias[i])) return false;
        return true;
    }
};
static_assert(sizeof(LayerColor)==32);
// Color scale/bias acts on straight, linear RGBA. Preserve the layer's
// premultiplication convention for the compositor that follows this step.
inline void apply_layer_color(float (&v)[4], const LayerColor& c, uint32_t flags) {
    const bool blend=(flags&2)!=0, premult=blend && !(flags&4);
    if (!blend) v[3]=1;
    if (premult) for(int i=0;i<3;++i) v[i]=v[3]>0 ? v[i]/v[3] : 0;
    for(int i=0;i<4;++i) v[i]=v[i]*c.scale[i]+c.bias[i];
    if (premult) for(int i=0;i<3;++i) v[i]*=v[3];
}
}
