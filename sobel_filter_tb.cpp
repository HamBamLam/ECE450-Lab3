
//----------------------------------------------------------
// tb_sobel_from_ppm.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cctype>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

typedef ap_axiu<24,1,1,1> axis24_t;

// DUT declaration
void sobel_rgb_axis(hls::stream<axis24_t> &in_axis,
                    hls::stream<axis24_t> &out_axis,
                    ap_uint<10> width, ap_uint<10> height);

// ----------- Simple PPM (P6) loader/saver -----------
static void skip_ws_and_comments(std::istream &is) {
    int c;
    while ((c = is.peek()) != EOF) {
        if (isspace(c)) { is.get(); continue; }
        if (c == '#') { // comment line
            std::string dummy;
            std::getline(is, dummy);
            continue;
        }
        break;
    }
}

static bool load_ppm_p6(const std::string &path, int &W, int &H, std::vector<unsigned char> &rgb) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "Cannot open: " << path << "\n"; return false; }

    std::string magic;
    f >> magic;
    if (magic != "P6") { std::cerr << "Not a P6 PPM: " << magic << "\n"; return false; }

    skip_ws_and_comments(f);
    f >> W; skip_ws_and_comments(f);
    f >> H; skip_ws_and_comments(f);
    int maxv; f >> maxv; f.get(); // consume single whitespace after maxv
    if (maxv != 255) { std::cerr << "Unsupported maxval (expected 255): " << maxv << "\n"; return false; }

    size_t nbytes = static_cast<size_t>(W) * static_cast<size_t>(H) * 3;
    rgb.resize(nbytes);
    f.read(reinterpret_cast<char*>(rgb.data()), nbytes);
    if (!f) { std::cerr << "Short read on pixel data.\n"; return false; }
    return true;
}

static bool save_ppm_p6(const std::string &path, int W, int H, const std::vector<unsigned char> &rgb) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << W << " " << H << "\n255\n";
    f.write(reinterpret_cast<const char*>(rgb.data()), (std::streamsize)rgb.size());
    return (bool)f;
}

// --------------- Testbench ---------------
int main() {
    const std::string in_path = "C:/Users/willi/Documents/Schoolwork/Vivado/sobel_resource/output.ppm"; // <-- put your local PPM file name here
    int W = 0, H = 0;
    std::vector<unsigned char> img; // RGBRGB...

    if (!load_ppm_p6(in_path, W, H, img)) {
        std::cerr << "Failed to load input PPM. Make sure it's P6, 8-bit, RGB.\n";
        return 1;
    }
    if (W != 512 || H != 512) {
        std::cout << "Note: loaded " << W << "x" << H << " (testbench works for any size, not just 512x512).\n";
    }

    hls::stream<axis24_t> s_in, s_out;

    // 1) Push image pixels into AXI4-Stream (scanline order)
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            size_t idx = (size_t)(y * W + x) * 3;
            unsigned char r = img[idx + 0];
            unsigned char g = img[idx + 1];
            unsigned char b = img[idx + 2];

            axis24_t t;
            t.data.range(23,16) = r;
            t.data.range(15, 8) = g;
            t.data.range( 7, 0) = b;

            t.keep = 0x7;  // 3 bytes valid
            t.strb = 0x7;
            t.user = (x == 0 && y == 0) ? 1 : 0;   // SOF
            t.last = (x == W - 1) ? 1 : 0;         // EOL
            t.id   = 0;
            t.dest = 0;

            s_in.write(t);
        }
    }
    ap_uint<10> width = (ap_uint<10>)W;
    ap_uint<10> height = (ap_uint<10>)H;
    // 2) Run DUT
    sobel_rgb_axis(s_in, s_out, width, height);

    // 3) Collect output
    std::vector<unsigned char> out( (size_t)W * H * 3 );
    int minv = 255, maxv = 0, sum = 0;
    for (int i = 0; i < W * H; ++i) {
        axis24_t t = s_out.read();
        unsigned char r = (unsigned char)t.data.range(23,16);
        unsigned char g = (unsigned char)t.data.range(15, 8);
        unsigned char b = (unsigned char)t.data.range( 7, 0);

        // Since SOBEL outputs grayscale replicated to RGB, r==g==b.
        if (r < minv) minv = r;
        if (r > maxv) maxv = r;
        sum += r;

        size_t idx = (size_t)i * 3;
        out[idx + 0] = r;
        out[idx + 1] = g;
        out[idx + 2] = b;
    }

    double mean = sum / (double)(W * H);
    std::cout << "Sobel stats: min=" << minv << " max=" << maxv << " mean=" << mean << "\n";

    const std::string out_path = "C:/Users/willi/Documents/Schoolwork/Vivado/sobel_resource/sobel_output.ppm";
    if (!save_ppm_p6(out_path, W, H, out)) {
        std::cerr << "Failed to write " << out_path << "\n";
        return 1;
    }
    std::cout << "Wrote " << out_path << ", open it with any viewer that supports PPM.\n";
    return 0;
}

