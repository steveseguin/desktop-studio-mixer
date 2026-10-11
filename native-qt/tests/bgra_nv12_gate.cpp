// Pixel-format correctness gate; actual recording workflows are in e2e/.
#include "versus/video/bgra_nv12.h"
#include <iostream>
#include <random>
#include <vector>

int main() {
    std::mt19937 random(20261011);
    for (int width : {1,2,3,4,6,7,8,14,127,1280}) for (int height : {1,2,3,8,17}) {
        const int stride = width*4+19;
        std::vector<uint8_t> input(stride*height+1);
        for (auto &value : input) value = static_cast<uint8_t>(random());
        const auto *src = input.data()+1;
        const size_t size = static_cast<size_t>(width)*height*3/2;
        std::vector<uint8_t> expected(size,128), actual(size+2,0xCD);
        for (int y=0; y<height; ++y) for (int x=0; x<width; ++x) {
            const auto *p=src+y*stride+x*4;
            expected[y*width+x]=uint8_t(((66*p[2]+129*p[1]+25*p[0]+128)>>8)+16);
        }
        for (int y=0; y+1<height; y+=2) for (int x=0; x+1<width; x+=2) {
            int rgb[3]{};
            for (int c=0; c<3; ++c) {
                for (int dy=0; dy<2; ++dy) for (int dx=0; dx<2; ++dx) rgb[c]+=src[(y+dy)*stride+(x+dx)*4+c];
                rgb[c]=(rgb[c]+2)/4;
            }
            const int offset=width*height+(y/2)*width+x;
            expected[offset]=uint8_t(((-38*rgb[2]-74*rgb[1]+112*rgb[0]+128)>>8)+128);
            expected[offset+1]=uint8_t(((112*rgb[2]-94*rgb[1]-18*rgb[0]+128)>>8)+128);
        }
        versus::video::detail::bgraToNv12(src,width,height,stride,actual.data()+1);
        if (actual.front()!=0xCD || actual.back()!=0xCD || !std::equal(expected.begin(),expected.end(),actual.begin()+1)) {
            std::cerr << "NV12 mismatch or overrun: " << width << 'x' << height << '\n'; return 1;
        }
    }
    // Exact black/white endpoints and neutral chroma, independent of random coverage.
    for (uint8_t level : {uint8_t(0),uint8_t(255)}) {
        std::vector<uint8_t> input(32,level), out(12);
        versus::video::detail::bgraToNv12(input.data(),4,2,16,out.data());
        for (int i=0; i<12; ++i) if (out[i] != (i<8 ? (level ? 235:16) : 128)) return 2;
    }
    std::cout << "NV12 pixel gate passed: strides, alignment, tails, chroma averaging, endpoints.\n";
}
