// sobel_rgb_axis.cpp  (Vitis HLS 2020.2+ / 2023.x)
// Streaming 24-bit RGB -> Sobel edges -> 24-bit RGB (grayscale edges)

#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

typedef ap_axiu<24,1,1,1> axis24_t;

static inline ap_uint<8> clip8(int v){
    if(v<0) return 0; if(v>255) return 255; return (ap_uint<8>)v;
}

// Integer-friendly luma: Y ˜ 0.299R + 0.587G + 0.114B
static inline ap_uint<8> rgb_to_gray(ap_uint<8> r, ap_uint<8> g, ap_uint<8> b){
    //77*255 + 150*255 + 29*255 = 255*256 = +-65280
    ap_int<17> y = ( 77*r + 150*g + 29*b ) >> 8;  // 77/256 ~0.30, 150/256 ~0.59, 29/256 ~0.11
    return (ap_uint<8>)y;
}

// Top: process exactly width*height pixels from AXI4-Stream
void sobel_rgb_axis(hls::stream<axis24_t> &in_axis,
                    hls::stream<axis24_t> &out_axis,
                    ap_uint<10> width, ap_uint<10> height) 
{

    #pragma HLS INTERFACE axis      port=in_axis
    #pragma HLS INTERFACE axis      port=out_axis

    const int MAX_W = 1024;                 // supports up to 1024 pixels/line
    static ap_uint<8> line0[MAX_W];
    static ap_uint<8> line1[MAX_W];


    // 3x3 sliding window (three rows × three columns)
    ap_uint<8> w0[3]={0,0,0}, w1[3]={0,0,0}, w2[3]={0,0,0};

    //tiny op to ignore inter-loop dependency
    #pragma HLS DEPENDENCE variable=line0 inter false
    #pragma HLS DEPENDENCE variable=line1 inter false
    // Clear line buffers for this frame
    init_cols:
    for(ap_uint<10> x=0; x<width; ++x){ //img is 610 x 540
        // #pragma HLS UNROLL factor=4
        #pragma HLS PIPELINE II=1
        #pragma HLS LOOP_TRIPCOUNT min=1 max=1024 //for synthesis estimates
        line0[x]=0; line1[x]=0;
    }

    // Main scan
    row_loop:
    for(ap_uint<10> y=0; y<height; ++y){ //610 x 540
        #pragma HLS LOOP_TRIPCOUNT min=1 max=1024
        // #pragma HLS PIPELINE II=1
        // #pragma HLS UNROLL factor=4
        // restart window at each new line
        w0[0]=w0[1]=w0[2]=0;
        w1[0]=w1[1]=w1[2]=0;
        w2[0]=w2[1]=w2[2]=0;

        col_loop:
        for(ap_uint<10> x=0; x<width; ++x){
            #pragma HLS PIPELINE II=1
            // #pragma HLS UNROLL factor=4
            #pragma HLS LOOP_TRIPCOUNT min=1 max=1024
            axis24_t din = in_axis.read();

            ap_uint<8> r = din.data.range(23,16);
            ap_uint<8> g = din.data.range(15, 8);
            ap_uint<8> b = din.data.range( 7, 0);
            ap_uint<8> gray = rgb_to_gray(r,g,b);

            // Fetch previous two rows at this column BEFORE updating
            ap_uint<8> top2 = line1[x];   // y-2
            ap_uint<8> top1 = line0[x];   // y-1

            // Shift window left
            w0[0]=w0[1]; w0[1]=w0[2];
            w1[0]=w1[1]; w1[1]=w1[2];
            w2[0]=w2[1]; w2[1]=w2[2];

            // Insert new rightmost column from the three rows
            w0[2] = top2;
            w1[2] = top1;
            w2[2] = gray;

            // Update line buffers (roll rows down)
            line1[x] = line0[x];
            line0[x] = gray;

            ap_int<11> gx = 0, gy = 0; // worst case ~4*255=1020
            // Only valid after we have at least 3 cols and 3 rows
            if(y >= 2 && x >= 2){
                // Sobel Gx
                gx =  - (int)w0[0] + (int)w0[2]
                      -2*(int)w1[0] + 2*(int)w1[2]
                      - (int)w2[0] + (int)w2[2];
                // Sobel Gy
                gy =  - (int)w0[0] -2*(int)w0[1] - (int)w0[2]
                       + (int)w2[0] +2*(int)w2[1] + (int)w2[2];
            }
            // ap_int<12> mag = (gx<0?-gx:gx) + (gy<0?-gy:gy); //at most 2 * 1020 = 2040
            ap_int<12> abs_gx = (gx < 0) ? (ap_int<12>)(-gx) : (ap_int<12>)gx; //weird compilation error fix
            ap_int<12> abs_gy = (gy < 0) ? (ap_int<12>)(-gy) : (ap_int<12>)gy;
            ap_uint<12> mag = abs_gx + abs_gy;
            if(mag > 255) mag = 255;
            ap_uint<8> edge = (y>=2 && x>=2) ? (ap_uint<8>)mag : (ap_uint<8>)0;

            axis24_t dout;
            dout.data.range(23,16) = edge;  // R
            dout.data.range(15, 8) = edge;  // G
            dout.data.range( 7, 0) = edge;  // B

            // Generate frame markers
            dout.user = (y==0 && x==0) ? 1 : 0;        // SOF
            dout.last = (x==width-1) ? 1 : 0;           // EOL
            dout.keep = din.keep;  dout.strb = din.strb;
            dout.id   = din.id;    dout.dest = din.dest;

            out_axis.write(dout);
        }
    }
}