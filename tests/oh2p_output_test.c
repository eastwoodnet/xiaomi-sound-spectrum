#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../oh2p_output.h"

int main(void) {
    uint32_t frame[18] = {0};
    oh2p_brightness = 100;
    /* A constant field must stay constant, with the original BGR encoding. */
    for (int i=0;i<18;i++) frame[i]=0x332211;
    for (int p=0;p<12;p++) assert(oh2p_pixel(frame,p)==0x332211);
    /* Every frequency/peak position must survive spatial reduction. */
    for (int v=0;v<18;v++) {
        for(int i=0;i<18;i++) frame[i]=0;
        frame[v]=0xFFFFFF;
        unsigned sum=0;
        for(int p=0;p<12;p++) sum+=oh2p_pixel(frame,p)&255;
        assert(sum==170);
    }
    /* The two ring endpoints split equally at the strip center and edges. */
    for (int i=0;i<18;i++) frame[i]=0;
    frame[0]=0xFFFFFF;
    for (int p=0;p<12;p++)
        assert(oh2p_pixel(frame,p)==((p==5 || p==6) ? 0x555555u : 0));
    frame[0]=0;
    frame[9]=0xFFFFFF;
    for (int p=0;p<12;p++)
        assert(oh2p_pixel(frame,p)==((p==0 || p==11) ? 0x555555u : 0));

    /* Stereo wings must reach the matching physical side when viewed front-on.
     * Red represents the left input; blue represents the right input. */
    for (int i=0;i<18;i++) frame[i]=0;
    for (int i=1;i<=8;i++) { frame[i]=0x0000FF; frame[18-i]=0xFF0000; }
    for (int p=0;p<12;p++) {
        uint32_t color=oh2p_pixel(frame,p);
        assert(color!=0);
        assert((color & (p<6 ? 0x00FFFFu : 0xFFFF00u))==0);
    }

    /* Equal stereo levels remain mirror-symmetric after resampling. */
    frame[0]=0x17314F;
    frame[9]=0xC48723;
    for (int i=1;i<=8;i++) frame[i]=frame[18-i]=(uint32_t)i*0x17230B;
    for (int p=0;p<6;p++) assert(oh2p_pixel(frame,p)==oh2p_pixel(frame,11-p));

    /* A mode-2-style spread grows outward from the center without dark gaps. */
    unsigned previous[12]={0};
    for (int spread=0;spread<=8;spread++) {
        for (int i=0;i<18;i++) frame[i]=0;
        frame[0]=0xFFFFFF;
        for (int i=1;i<=spread;i++) frame[i]=frame[18-i]=0xFFFFFF;
        for (int p=0;p<12;p++) {
            unsigned level=oh2p_pixel(frame,p)&255;
            assert(level>=previous[p]);
            previous[p]=level;
            assert(level==(oh2p_pixel(frame,11-p)&255));
            if (p<5) assert(level<=(oh2p_pixel(frame,p+1)&255));
        }
    }

    for (int i=0;i<18;i++) frame[i]=0xFFFFFF;
    oh2p_brightness=20;
    for(int p=0;p<12;p++) assert(oh2p_pixel(frame,p)==0x333333);
    puts("OH2P mapping: colors, all inputs, center/edges, stereo, symmetry, outward spread and brightness passed");
    return 0;
}
