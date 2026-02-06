
//#include "Image.h"


//int main(int argc, char** argv) {
//	Image test("imgs/test1.jpg");

//	return 0;
//}


//Code for edge detector:

#include "Image.h"
#include <cstdlib>
#include <cmath>
#include <chrono>

/**
 * MAIN EXECUTION BLOCK
 * This program demonstrates a "Canny-style" edge detection pipeline.
 */
int main(int argc, char** argv) {

	// 1. LOAD THE IMAGE
	Image img("imgs/test6.png");

	// 2. GRAYSCALE CONVERSION
	// Edge detection doesn't need color, only changes in brightness.
	img.grayscale_avg(); 
	int img_size = img.w * img.h;

	// Create a single-channel (B&W) image to save memory
	Image gray_img(img.w, img.h, 1);
	for(uint64_t k=0; k<img_size; ++k) {
		// Extract only the first channel (Red/Gray) from the original
		gray_img.data[k] = img.data[img.channels * k];
	}
	gray_img.write("imgs/test6_gray.png");

	// 3. NOISE REDUCTION (BLUR)
	// We blur the image to ensure the detector finds "real" edges, not digital noise.
	Image blur_img(img.w, img.h, 1);
	double gauss[9] = {
		1/16., 2/16., 1/16.,
		2/16., 4/16., 2/16.,
		1/16., 2/16., 1/16.
	};
	// convolve_linear applies the blur matrix to the pixels
	gray_img.convolve_linear(0, 3, 3, gauss, 1, 1);
	for(uint64_t k=0; k<img_size; ++k) {
		blur_img.data[k] = gray_img.data[k];
	}
	blur_img.write("imgs/test6_blur.png");

	// 4. CALCULATING GRADIENTS (The "Change" in light)
	// gx = Horizontal change (Left to Right)
	// gy = Vertical change (Top to Bottom)
	double* tx = new double[img_size];
	double* ty = new double[img_size];
	double* gx = new double[img_size];
	double* gy = new double[img_size];

	// SEPARABLE CONVOLUTION LOGIC
	// Instead of a slow 2D pass, we do a horizontal pass (tx, ty) 
	// followed by a vertical pass (gx, gy) for maximum speed.
	for(uint32_t c=1; c<blur_img.w-1; ++c) {
		for(uint32_t r=0; r<blur_img.h; ++r) {
			// Find difference between right and left pixels
			tx[r*blur_img.w+c] = blur_img.data[r*blur_img.w+c+1] - blur_img.data[r*blur_img.w+c-1];
			// Smooth the vertical data to prepare for gy
			ty[r*blur_img.w+c] = 47*blur_img.data[r*blur_img.w+c+1] + 162*blur_img.data[r*blur_img.w+c] + 47*blur_img.data[r*blur_img.w+c-1];
		}
	}
	for(uint32_t c=1; c<blur_img.w-1; ++c) {
		for(uint32_t r=1; r<blur_img.h-1; ++r) {
			// Finalize horizontal change
			gx[r*blur_img.w+c] = 47*tx[(r+1)*blur_img.w+c] + 162*tx[r*blur_img.w+c] + 47*tx[(r-1)*blur_img.w+c];
			// Finalize vertical change
			gy[r*blur_img.w+c] = ty[(r+1)*blur_img.w+c] - ty[(r-1)*blur_img.w+c];
		}
	}

	delete[] tx; // Free temporary math memory
	delete[] ty;

	// 5. GENERATE COMPONENT IMAGES
	// These images show purely horizontal and purely vertical edges.
	double mxx = -INFINITY, mxy = -INFINITY, mnx = INFINITY, mny = INFINITY;
	for(uint64_t k=0; k<img_size; ++k) {
		mxx = fmax(mxx, gx[k]); mxy = fmax(mxy, gy[k]);
		mnx = fmin(mnx, gx[k]); mny = fmin(mny, gy[k]);
	}
	Image Gx(img.w, img.h, 1);
	Image Gy(img.w, img.h, 1);
	for(uint64_t k=0; k<img_size; ++k) {
		Gx.data[k] = (uint8_t)(255*(gx[k]-mnx)/(mxx-mnx));
		Gy.data[k] = (uint8_t)(255*(gy[k]-mny)/(mxy-mny));
	}
	Gx.write("imgs/Gx.png");
	Gy.write("imgs/Gy.png");

	// 6. CALCULATE MAGNITUDE AND DIRECTION
	double threshold = 0.09; // Ignore weak changes below this value
	double* g = new double[img_size];
	double* theta = new double[img_size];
	for(uint64_t k=0; k<img_size; ++k) {
		double x = gx[k];
		double y = gy[k];
		g[k] = sqrt(x*x + y*y);   // Pythagoras: Total edge strength
		theta[k] = atan2(y, x);  // Find the angle of the edge
	}

	// 7. COLOR-CODING THE EDGES
	// We convert the edge angle (theta) into a color (Hue) 
	// so we can "see" which way the lines are leaning.
	Image G(img.w, img.h, 1);
	Image GT(img.w, img.h, 3);

	double mx = -INFINITY, mn = INFINITY;
	for(uint64_t k=0; k<img_size; ++k) {
		mx = fmax(mx, g[k]);
		mn = fmin(mn, g[k]);
	}

	for(uint64_t k=0; k<img_size; ++k) {
		double h = theta[k]*180./M_PI + 180.; // Map angle to 0-360 degrees
		double v;
		if(mx == mn) v = 0;
		else v = (g[k]-mn)/(mx-mn) > threshold ? (g[k]-mn)/(mx-mn) : 0;

		// HSL to RGB conversion math
		double s = v, l = v;
		double c = (1-abs(2*l-1))*s;
		double x_color = c*(1-abs(fmod((h/60),2)-1));
		double m_adj = l-c/2;

		double rt=0, gt_c=0, bt=0;
		if(h < 60) { rt = c; gt_c = x_color; }
		else if(h < 120) { rt = x_color; gt_c = c; }
		else if(h < 180) { gt_c = c; bt = x_color; }
		else if(h < 240) { gt_c = x_color; bt = c; }
		else if(h < 300) { bt = c; rt = x_color; }
		else { bt = x_color; rt = c; }

		GT.data[k*3] = (uint8_t)(255*(rt+m_adj));
		GT.data[k*3+1] = (uint8_t)(255*(gt_c+m_adj));
		GT.data[k*3+2] = (uint8_t)(255*(bt+m_adj));

		G.data[k] = (uint8_t)(255*v); // Final B&W outline
	}
	G.write("imgs/G.png");
	GT.write("imgs/GT.png");

	// CLEAN UP
	delete[] gx; delete[] gy; delete[] g; delete[] theta;
	return 0;
}








