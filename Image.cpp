#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
// This macro ensures pixel values stay between 0-255 (standard for 8-bit images)
#define BYTE_BOUND(value) value < 0 ? 0 : (value > 255 ? 255 : value)

#include "stb_image.h"       // Library for loading JPG, PNG, etc.
#include "stb_image_write.h" // Library for saving images
#include "Image.h"

// --- CONSTRUCTORS & DESTRUCTOR ---

// Constructor: Load image from a file
Image::Image(const char* filename, int channel_force) {
	if(read(filename, channel_force)) {
		printf("Read %s\n", filename);
		size = w*h*channels; // Total number of bytes (Width * Height * Color Channels)
	}
	else {
		printf("Failed to read %s\n", filename);
	}
}

// Constructor: Create a blank image with specific dimensions
Image::Image(int w, int h, int channels) : w(w), h(h), channels(channels) {
	size = w*h*channels;
	data = new uint8_t[size]; // Allocate memory for pixels
}

// Copy Constructor: Create a deep copy of an existing Image object
Image::Image(const Image& img) : Image(img.w, img.h, img.channels) {
	memcpy(data, img.data, size); // Copy the raw pixel data buffer
}

// Destructor: Clean up memory
Image::~Image() {
	stbi_image_free(data); // Free the memory allocated by stb_image or 'new'
}

// --- FILE I/O ---

// Read an image file into the 'data' buffer
bool Image::read(const char* filename, int channel_force) {
	data = stbi_load(filename, &w, &h, &channels, channel_force);
	channels = channel_force == 0 ? channels : channel_force;
	return data != NULL;
}

// Write the 'data' buffer to a file based on extension
bool Image::write(const char* filename) {
	ImageType type = get_file_type(filename);
	int success;
  switch (type) {
    case PNG: success = stbi_write_png(filename, w, h, channels, data, w*channels); break;
    case BMP: success = stbi_write_bmp(filename, w, h, channels, data); break;
    case JPG: success = stbi_write_jpg(filename, w, h, channels, data, 100); break;
    case TGA: success = stbi_write_tga(filename, w, h, channels, data); break;
  }
  if(success != 0) {
    printf("\e[32mWrote \e[36m%s\e[0m, %d, %d, %d, %zu\n", filename, w, h, channels, size);
    return true;
  }
  return false;
}

// Helper to determine file type from extension
ImageType Image::get_file_type(const char* filename) {
	const char* ext = strrchr(filename, '.');
	if(ext != nullptr) {
		if(strcmp(ext, ".png") == 0) return PNG;
		if(strcmp(ext, ".jpg") == 0) return JPG;
		if(strcmp(ext, ".bmp") == 0) return BMP;
		if(strcmp(ext, ".tga") == 0) return TGA;
	}
	return PNG; // Default to PNG
}

// --- SPATIAL CONVOLUTION (Filtering) ---

/* Logic for Convolution:
   We slide a 'kernel' (filter matrix) over the image. 
   Each new pixel is a weighted average of its neighbors.
*/

// Option 1: Clamp to 0 (Pixels outside the border are treated as Black)
Image& Image::std_convolve_clamp_to_0(uint8_t channel, uint32_t ker_w, uint32_t ker_h, double ker[], uint32_t cr, uint32_t cc) {
	uint8_t new_data[w*h]; // Temporary buffer for the filtered channel
	uint64_t center = cr*ker_w + cc; // Identify center of the kernel
	
	for(uint64_t k=channel; k<size; k+=channels) {
		double c = 0;
		// Iterate over the kernel dimensions
		for(long i = -((long)cr); i<(long)ker_h-cr; ++i) {
			long row = ((long)k/channels)/w-i;
			if(row < 0 || row > h-1) continue; // Out of bounds = skip (Black)
			
			for(long j = -((long)cc); j<(long)ker_w-cc; ++j) {
				long col = ((long)k/channels)%w-j;
				if(col < 0 || col > w-1) continue; // Out of bounds = skip
				
				// Multiply kernel weight by pixel value and add to sum
				c += ker[center+i*(long)ker_w+j]*data[(row*w+col)*channels+channel];
			}
		}
		new_data[k/channels] = (uint8_t)BYTE_BOUND(round(c)); // Keep value in 0-255 range
	}
	// Copy processed data back to original image buffer
	for(uint64_t k=channel; k<size; k+=channels) {
		data[k] = new_data[k/channels];
	}
	return *this;
}

// Option 2: Clamp to Border (Pixels outside use the color of the edge pixel)
Image& Image::std_convolve_clamp_to_border(uint8_t channel, uint32_t ker_w, uint32_t ker_h, double ker[], uint32_t cr, uint32_t cc) {
	uint8_t new_data[w*h];
	uint64_t center = cr*ker_w + cc;
	for(uint64_t k=channel; k<size; k+=channels) {
		double c = 0;
		for(long i = -((long)cr); i<(long)ker_h-cr; ++i) {
			long row = ((long)k/channels)/w-i;
			if(row < 0) row = 0; // Stick to top edge
			else if(row > h-1) row = h-1; // Stick to bottom edge
			
			for(long j = -((long)cc); j<(long)ker_w-cc; ++j) {
				long col = ((long)k/channels)%w-j;
				if(col < 0) col = 0; // Stick to left edge
				else if(col > w-1) col = w-1; // Stick to right edge
				
				c += ker[center+i*(long)ker_w+j]*data[(row*w+col)*channels+channel];
			}
		}
		new_data[k/channels] = (uint8_t)BYTE_BOUND(round(c));
	}
	for(uint64_t k=channel; k<size; k+=channels) {
		data[k] = new_data[k/channels];
	}
	return *this;
}

// Option 3: Cyclic (Wrapping - Top edge connects to Bottom, Left to Right)
Image& Image::std_convolve_cyclic(uint8_t channel, uint32_t ker_w, uint32_t ker_h, double ker[], uint32_t cr, uint32_t cc) {
	uint8_t new_data[w*h];
	uint64_t center = cr*ker_w + cc;
	for(uint64_t k=channel; k<size; k+=channels) {
		double c = 0;
		for(long i = -((long)cr); i<(long)ker_h-cr; ++i) {
			long row = ((long)k/channels)/w-i;
			if(row < 0) row = row%h + h; // Wrap to bottom
			else if(row > h-1) row %= h; // Wrap to top
			
			for(long j = -((long)cc); j<(long)ker_w-cc; ++j) {
				long col = ((long)k/channels)%w-j;
				if(col < 0) col = col%w + w; // Wrap to right
				else if(col > w-1) col %= w; // Wrap to left
				
				c += ker[center+i*(long)ker_w+j]*data[(row*w+col)*channels+channel];
			}
		}
		new_data[k/channels] = (uint8_t)BYTE_BOUND(round(c));
	}
	for(uint64_t k=channel; k<size; k+=channels) {
		data[k] = new_data[k/channels];
	}
	return *this;
}

// --- FREQUENCY DOMAIN (FFT MATH) ---

// Bit Reversal: Necessary for the Cooley-Tukey FFT algorithm optimization
uint32_t Image::rev(uint32_t n, uint32_t a) {
	uint8_t max_bits = (uint8_t)ceil(log2(n));
	uint32_t reversed_a = 0;
	for(uint8_t i=0; i<max_bits; ++i) {
		if(a & (1<<i)) {
			reversed_a |= (1<<(max_bits-1-i));
		}
	}
	return reversed_a;
}

// Rearrange data in bit-reversed order
void Image::bit_rev(uint32_t n, std::complex<double> a[], std::complex<double>* A) {
	for(uint32_t i=0; i<n; ++i) {
		A[rev(n,i)] = a[i];
	}
}

// FAST FOURIER TRANSFORM (FFT)
// Converts image pixels into Frequency data (Speed: N log N)
void Image::fft(uint32_t n, std::complex<double> x[], std::complex<double>* X) {
	if(x != X) {
		memcpy(X, x, n*sizeof(std::complex<double>));
	}

	uint32_t sub_probs = 1;
	uint32_t sub_prob_size = n;
	uint32_t half, i, j_begin, j_end, j;
	std::complex<double> w_step, w, tmp1, tmp2;

	// Butterfly computation loop
	while(sub_prob_size>1) {
		half = sub_prob_size>>1;
		w_step = std::complex<double>(cos(-2*M_PI/sub_prob_size), sin(-2*M_PI/sub_prob_size));
		for(i=0; i<sub_probs; ++i) {
			j_begin = i*sub_prob_size;
			j_end = j_begin+half;
			w = std::complex<double>(1,0);
			for(j=j_begin; j<j_end; ++j) {
				tmp1 = X[j];
				tmp2 = X[j+half];
				X[j] = tmp1+tmp2; // Standard FFT butterfly addition
				X[j+half] = (tmp1-tmp2)*w; // Standard FFT butterfly subtraction + twiddle factor
				w *= w_step;
			}
		}
		sub_probs <<= 1;
		sub_prob_size = half;
	}
}


// --- INVERSE FAST FOURIER TRANSFORM (IFFT) ---
// This turns frequency data back into normal image pixels.
void Image::ifft(uint32_t n, std::complex<double> X[], std::complex<double>* x) {
	// If the input and output pointers are different, copy the data first.
	if(X != x) {
		memcpy(x, X, n*sizeof(std::complex<double>));
	}

	uint32_t sub_probs = n>>1;
	uint32_t sub_prob_size;
	uint32_t half = 1;
	// Logic variables for the "Butterfly" algorithm
	uint32_t i, j_begin, j_end, j;
	std::complex<double> w_step, w, tmp1, tmp2;

	// Loop through the levels of the FFT tree
	while(half<n) {
		sub_prob_size = half<<1;
		// w_step is the "Twiddle Factor" - it uses basic trigonometry (cos/sin)
		// for rotating values in the complex plane.
		w_step = std::complex<double>(cos(2*M_PI/sub_prob_size), sin(2*M_PI/sub_prob_size));
		for(i=0; i<sub_probs; ++i) {
			j_begin = i*sub_prob_size;
			j_end = j_begin+half;
			w = std::complex<double>(1,0);
			for(j=j_begin; j<j_end; ++j) {
				tmp1 = x[j];
				tmp2 = w*x[j+half];
				x[j] = tmp1+tmp2;      // Summing the frequencies
				x[j+half] = tmp1-tmp2; // Subtracting the frequencies
				w *= w_step;           // Step to the next rotation
			}
		}
		sub_probs >>= 1;
		half = sub_prob_size;
	}
	// Final Step: Divide everything by 'n' to normalize the values.
	for(uint32_t i=0; i<n; ++i) {
		x[i] /= n;
	}
}

// --- 2D DISCRETE FOURIER TRANSFORM ---
// Images are 2D (rows and columns). We run the 1D FFT on every row, 
// then run it again on every column.
void Image::dft_2D(uint32_t m, uint32_t n, std::complex<double> x[], std::complex<double>* X) {
	std::complex<double>* intermediate = new std::complex<double>[m*n];
	
	// Step 1: Process every Row
	for(uint32_t i=0; i<m; ++i) {
		fft(n, x+i*n, intermediate+i*n);
	}
	
	// Step 2: Switch Rows to Columns (Transpose) and process every Column
	for(uint32_t j=0; j<n; ++j) {
		for(uint32_t i=0; i<m; ++i) {
			X[j*m+i] = intermediate[i*n+j]; 
		}
		fft(m, X+j*m, X+j*m);
	}
	delete[] intermediate; // Clean up temporary memory
}

// --- KERNEL PADDING ---
// To use FFT for convolution, the "Filter" (kernel) must be the same size 
// as the image. This fills the empty space with zeros.
void Image::pad_kernel(uint32_t ker_w, uint32_t ker_h, double ker[], uint32_t cr, uint32_t cc, uint32_t pw, uint32_t ph, std::complex<double>* pad_ker) {
	for(long i=-((long)cr); i<(long)ker_h-cr; ++i) {
		uint32_t r = (i<0) ? i+ph : i;
		for(long j=-((long)cc); j<(long)ker_w-cc; ++j) {
			uint32_t c = (j<0) ? j+pw : j;
			// Copy original filter value into the big padded matrix
			pad_ker[r*pw+c] = std::complex<double>(ker[(i+cr)*ker_w+(j+cc)], 0);
		}
	}
}

// --- FAST CONVOLUTION (THE "MAGIC" STEP) ---
// This is the implementation of the logic we discussed earlier.
Image& Image::fd_convolve_clamp_to_0(uint8_t channel, uint32_t ker_w, uint32_t ker_h, double ker[], uint32_t cr, uint32_t cc) {
	// 1. Calculate the necessary padding size (must be a power of 2 for FFT speed)
	uint32_t pw = 1<<((uint8_t)ceil(log2(w+ker_w-1)));
	uint32_t ph = 1<<((uint8_t)ceil(log2(h+ker_h-1)));
	uint64_t psize = pw*ph;

	// 2. Prepare the Image and Kernel by padding them with zeros
	std::complex<double>* pad_img = new std::complex<double>[psize];
	std::complex<double>* pad_ker = new std::complex<double>[psize];
	
	// Copy image pixels into the complex number buffer
	for(uint32_t i=0; i<h; ++i) {
		for(uint32_t j=0; j<w; ++j) {
			pad_img[i*pw+j] = std::complex<double>(data[(i*w+j)*channels+channel],0);
		}
	}
	pad_kernel(ker_w, ker_h, ker, cr, cc, pw, ph, pad_ker);

	// 3. TRANSFORM: Convert both to Frequency Domain
	dft_2D(ph, pw, pad_img, pad_img);
	dft_2D(ph, pw, pad_ker, pad_ker);

	// 4. MULTIPLY: This is the Convolution Theorem in action!
	pointwise_product(psize, pad_img, pad_ker, pad_img);

	// 5. INVERT: Convert the result back to normal pixels
	idft_2D(ph, pw, pad_img, pad_img);

	// 6. UPDATE: Copy the final math results back into the actual Image data
	for(uint32_t i=0; i<h; ++i) {
		for(uint32_t j=0; j<w; ++j) {
			data[(i*w+j)*channels+channel] = BYTE_BOUND((uint8_t)round(pad_img[i*pw+j].real()));
		}
	}

	return *this;
}

/**
 * FREQUENCY DOMAIN CONVOLUTION (CLAMP TO BORDER)
 * This method applies a filter (kernel) by repeating the edge pixels 
 * when the filter goes outside the boundaries of the image.
 */
Image& Image::fd_convolve_clamp_to_border(uint8_t channel, uint32_t ker_w, uint32_t ker_h, double ker[], uint32_t cr, uint32_t cc) {
	// 1. CALCULATE PADDING
	// FFT (Fast Fourier Transform) math is fastest when dimensions are powers of 2 (2, 4, 8, 16...).
	// This code finds the smallest power of 2 that can fit the image plus the filter size.
	uint32_t pw = 1<<((uint8_t)ceil(log2(w+ker_w-1)));
	uint32_t ph = 1<<((uint8_t)ceil(log2(h+ker_h-1)));
	uint64_t psize = pw*ph; // Total number of complex numbers needed in memory

	// 2. PAD IMAGE (The "Border" Logic)
	// Create a larger temporary canvas. If we go past the real image edge,
	// we grab the color of the last real pixel available (h-1 or w-1).
	std::complex<double>* pad_img = new std::complex<double>[psize];
	for(uint32_t i=0; i<ph; ++i) {
		uint32_t r = (i<h) ? i : ((i<h+cr ? h-1 : 0)); // Stick to top/bottom edge
		for(uint32_t j=0; j<pw; ++j) {
			uint32_t c = (j<w) ? j : ((j<w+cc ? w-1 : 0)); // Stick to left/right edge
			pad_img[i*pw+j] = std::complex<double>(data[(r*w+c)*channels+channel],0);
		}
	}

	// 3. PAD KERNEL
	// The filter must be resized to match the image size for the math to work.
	std::complex<double>* pad_ker = new std::complex<double>[psize];
	pad_kernel(ker_w, ker_h, ker, cr, cc, pw, ph, pad_ker);

	// 4. CONVOLUTION THEOREM (THE MATH SHORTCUT)
	dft_2D(ph, pw, pad_img, pad_img);           // Convert Image to Frequency Domain
	dft_2D(ph, pw, pad_ker, pad_ker);           // Convert Filter to Frequency Domain
	pointwise_product(psize, pad_img, pad_ker, pad_img); // Multiply them (instant convolution)
	idft_2D(ph, pw, pad_img, pad_img);          // Convert back to normal pixels

	// 5. UPDATE ORIGINAL DATA
	// Take the real-number results from the math and save them back as pixel colors.
	for(uint32_t i=0; i<h; ++i) {
		for(uint32_t j=0; j<w; ++j) {
			data[(i*w+j)*channels+channel] = BYTE_BOUND((uint8_t)round(pad_img[i*pw+j].real()));
		}
	}

	return *this;
}

/**
 * FREQUENCY DOMAIN CONVOLUTION (CYCLIC)
 * Instead of repeating edges, this "wraps" the image like a globe. 
 * The top edge looks at the bottom, and the left looks at the right.
 */
Image& Image::fd_convolve_cyclic(uint8_t channel, uint32_t ker_w, uint32_t ker_h, double ker[], uint32_t cr, uint32_t cc) {
	uint32_t pw = 1<<((uint8_t)ceil(log2(w+ker_w-1)));
	uint32_t ph = 1<<((uint8_t)ceil(log2(h+ker_h-1)));
	uint64_t psize = pw*ph;

	// PAD IMAGE (The "Wrap" Logic)
	std::complex<double>* pad_img = new std::complex<double>[psize];
	for(uint32_t i=0; i<ph; ++i) {
		// Uses the modulo operator (%) to jump from one side of the image to the other
		uint32_t r = (i<h) ? i : ((i<h+cr ? i%h : h-ph+i)); 
		for(uint32_t j=0; j<pw; ++j) {
			uint32_t c = (j<w) ? j : ((j<w+cc ? j%w : w-pw+j));
			pad_img[i*pw+j] = std::complex<double>(data[(r*w+c)*channels+channel],0);
		}
	}

	// [Steps 3, 4, and 5 are identical to the Border version above]
	std::complex<double>* pad_ker = new std::complex<double>[psize];
	pad_kernel(ker_w, ker_h, ker, cr, cc, pw, ph, pad_ker);
	dft_2D(ph, pw, pad_img, pad_img);
	dft_2D(ph, pw, pad_ker, pad_ker);
	pointwise_product(psize, pad_img, pad_ker, pad_img);
	idft_2D(ph, pw, pad_img, pad_img);

	for(uint32_t i=0; i<h; ++i) {
		for(uint32_t j=0; j<w; ++j) {
			data[(i*w+j)*channels+channel] = BYTE_BOUND((uint8_t)round(pad_img[i*pw+j].real()));
		}
	}

	return *this;
}

/**
 * SMART DISPATCHING
 * These functions decide which method to use.
 * If the filter is small, standard "sliding window" is faster.
 * If the filter is large (> 224 pixels total), "Frequency Domain" is much faster.
 */
Image& Image::convolve_linear(uint8_t channel, uint32_t ker_w, uint32_t ker_h, double ker[], uint32_t cr, uint32_t cc) {
	if(ker_w*ker_h > 224) {
		return fd_convolve_clamp_to_0(channel, ker_w, ker_h, ker, cr, cc);
	}
	else {
		return std_convolve_clamp_to_0(channel, ker_w, ker_h, ker, cr, cc);
	}
}

// ... (Other smart dispatch functions follow the same logic for Border and Cyclic modes)

/**
 * DIFFMAP (Comparing Images)
 * Subtracts one image from another. Identical pixels become Black (0).
 * This is exactly how motion detection in security cameras works.
 */
Image& Image::diffmap(Image& img) {
	// Find the smallest shared dimensions between the two images
	int compare_width = fmin(w,img.w);
	int compare_height = fmin(h,img.h);
	int compare_channels = fmin(channels,img.channels);

	for(uint32_t i=0; i<compare_height; ++i) {
		for(uint32_t j=0; j<compare_width; ++j) {
			for(uint8_t k=0; k<compare_channels; ++k) {
				// Result = |Color A - Color B|
				data[(i*w+j)*channels+k] = BYTE_BOUND(abs(data[(i*w+j)*channels+k] - img.data[(i*img.w+j)*img.channels+k]));
			}
		}
	}
	return *this;
}

/**
 * DIFFMAP SCALE
 * Similar to Diffmap, but it "amplifies" the differences. 
 * If the changes are very subtle, this scales them up so they are bright and visible.
 */
Image& Image::diffmap_scale(Image& img, uint8_t scl) {
	int compare_width = fmin(w,img.w);
	int compare_height = fmin(h,img.h);
	int compare_channels = fmin(channels,img.channels);
	uint8_t largest = 0;

	// First pass: Find the absolute difference and identify the largest change
	for(uint32_t i=0; i<compare_height; ++i) {
		for(uint32_t j=0; j<compare_width; ++j) {
			for(uint8_t k=0; k<compare_channels; ++k) {
				data[(i*w+j)*channels+k] = BYTE_BOUND(abs(data[(i*w+j)*channels+k] - img.data[(i*img.w+j)*img.channels+k]));
				largest = fmax(largest, data[(i*w+j)*channels+k]);
			}
		}
	}

	// Second pass: Boost the brightness of every difference found
	scl = 255/fmax(1, fmax(scl, largest));
	for(int i=0; i<size; ++i) {
		data[i] *= scl;
	}
	return *this;
}


/**
 * GRAYSCALE AVERAGE
 * Converts a color image to black and white by simply averaging the 
 * Red, Green, and Blue values for every pixel.
 */
Image& Image::grayscale_avg() {
	// Check if the image has at least 3 channels (Red, Green, Blue)
	if(channels < 3) {
		printf("Image %p has less than 3 channels, it is assumed to already be grayscale.", this);
	}
	else {
		// Loop through every pixel in the raw data buffer
		for(int i = 0; i < size; i+=channels) {
			// Formula: gray = (R + G + B) / 3
			int gray = (data[i] + data[i+1] + data[i+2])/3;
			
			// Set Red, Green, and Blue all to the same 'gray' value
			// memset writes the value across the next 3 bytes in memory
			memset(data+i, gray, 3);
		}
	}
	return *this;
}

/**
 * GRAYSCALE LUMINANCE
 * A more advanced B&W conversion that accounts for human biology.
 * Humans perceive Green more brightly than Blue. This formula weights 
 * the colors to create a more "natural" looking grayscale image.
 */
Image& Image::grayscale_lum() {
	if(channels < 3) {
		printf("Image %p has less than 3 channels, it is assumed to already be grayscale.", this);
	}
	else {
		for(int i = 0; i < size; i+=channels) {
			// Weights: Red (21.26%), Green (71.52%), Blue (7.22%)
			int gray = 0.2126*data[i] + 0.7152*data[i+1] + 0.0722*data[i+2];
			memset(data+i, gray, 3);
		}
	}
	return *this;
}

/**
 * COLOR MASK
 * Multiplies each color channel by a float (0.0 to 1.0).
 * Use this to "tint" an image. Ex: color_mask(1.0, 0.5, 0.5) makes it look redder.
 */
Image& Image::color_mask(float r, float g, float b) {
	if(channels < 3) {
		printf("\e[31m[ERROR] Color mask requires at least 3 channels\e[0m\n");
	}
	else {
		for(int i = 0; i < size; i+=channels) {
			data[i]   *= r; // Red channel
			data[i+1] *= g; // Green channel
			data[i+2] *= b; // Blue channel
		}
	}
	return *this;
}

/**
 * ENCODE MESSAGE (Steganography)
 * Hides a text message inside the pixels by changing the Least Significant Bit (LSB).
 * This change is so tiny that the human eye cannot see any difference in color.
 */
Image& Image::encodeMessage(const char* message) {
	// Calculate total bits (8 bits per character)
	uint32_t len = strlen(message) * 8;
	
	// Safety Check: Is the image big enough to hold the message?
	if(len + STEG_HEADER_SIZE > size) {
		printf("\e[31m[ERROR] This message is too large\e[0m\n");
		return *this;
	}

	// STEP 1: Encode the length of the message into the first pixels (The Header)
	for(uint8_t i = 0; i < STEG_HEADER_SIZE; ++i) {
		data[i] &= 0xFE; // Clear the last bit (0xFE = 11111110)
		// Extract the specific bit from 'len' and put it in the pixel's last bit
		data[i] |= (len >> (STEG_HEADER_SIZE - 1 - i)) & 1UL;
	}

	// STEP 2: Encode the actual message bits into the following pixels
	for(uint32_t i = 0; i < len; ++i) {
		// Calculate the correct pixel index, starting after the header
		data[i+STEG_HEADER_SIZE] &= 0xFE; 
		// Hide 1 bit of the message inside the current pixel
		data[i+STEG_HEADER_SIZE] |= (message[i/8] >> ((len-1-i)%8)) & 1;
	}

	return *this;
}

/**
 * DECODE MESSAGE
 * Reads the last bit of every pixel to rebuild the hidden text message.
 */
Image& Image::decodeMessage(char* buffer, size_t* messageLength) {
	uint32_t len = 0;
	
	// STEP 1: Read the first pixels to find out how long the hidden message is
	for(uint8_t i = 0; i < STEG_HEADER_SIZE; ++i) {
		len = (len << 1) | (data[i] & 1);
	}
	*messageLength = len / 8; // Convert bits back to character count

	// STEP 2: Extract the bits from the pixels and rebuild the characters
	for(uint32_t i = 0; i < len; ++i) {
		// Shift current bits left and add the new bit found in the pixel
		buffer[i/8] = (buffer[i/8] << 1) | (data[i+STEG_HEADER_SIZE] & 1);
	}

	return *this;
}



/**
 * FLIP X (Horizontal Mirror)
 * Imagine looking at the image in a mirror. This swaps the left side pixels with the right side.
 */
Image& Image::flipX() {
    uint8_t tmp[4]; // A small temporary "box" to hold pixel data during the swap
    for(int y = 0; y < h; ++y) { // Go through every row from top to bottom
        for(int x = 0; x < w/2; ++x) { // Go through only half the width (because we swap with the other half)
            // Find the pixel on the left side
            uint8_t* px1 = &data[(x + y * w) * channels];
            // Find the matching pixel on the right side
            uint8_t* px2 = &data[((w - 1 - x) + y * w) * channels];
            
            // Swap them: Move left to temp box -> Move right to left -> Move temp box to right
            memcpy(tmp, px1, channels);
            memcpy(px1, px2, channels);
            memcpy(px2, tmp, channels);
        }
    }
    return *this;
}

/**
 * FLIP Y (Vertical Mirror)
 * This turns the image upside down by swapping the top pixels with the bottom pixels.
 */
Image& Image::flipY() {
    uint8_t tmp[4];
    for(int x = 0; x < w; ++x) { // Go through every column from left to right
        for(int y = 0; y < h/2; ++y) { // Go through only half the height
            // Find the pixel on the top
            uint8_t* px1 = &data[(x + y * w) * channels];
            // Find the matching pixel on the bottom
            uint8_t* px2 = &data[(x + (h - 1 - y) * w) * channels];

            // Perform the swap
            memcpy(tmp, px1, channels);
            memcpy(px1, px2, channels);
            memcpy(px2, tmp, channels);
        }
    }
    return *this;
}







/**
 * OVERLAY (Sticker/Logo placement)
 * Places a smaller "source" image on top of this image at a specific (x, y) spot.
 * It handles transparency (Alpha) so the background shows through see-through parts.
 */
Image& Image::overlay(const Image& source, int x, int y) {
    for(int sy = 0; sy < source.h; ++sy) {
        if(sy + y < 0) {continue;} // Skip if the sticker is off-screen (top)
        else if(sy + y >= h) {break;} // Stop if we go off-screen (bottom)
        for(int sx = 0; sx < source.w; ++sx) {
            if(sx + x < 0) {continue;} // Skip if off-screen (left)
            else if(sx + x >= w) {break;} // Stop if off-screen (right)

            // Find the specific pixels on both the sticker and the background
            uint8_t* srcPx = &source.data[(sx + sy * source.w) * source.channels];
            uint8_t* dstPx = &data[(sx + x + (sy + y) * w) * channels];

            // Check how see-through the sticker is (Alpha value)
            float srcAlpha = source.channels < 4 ? 1 : srcPx[3] / 255.f;
            float dstAlpha = channels < 4 ? 1 : dstPx[3] / 255.f;

            // If the sticker is solid (not see-through), just overwrite the background
            if(srcAlpha > .99 && dstAlpha > .99) {
                if(source.channels >= channels) {
                    memcpy(dstPx, srcPx, channels);
                } else {
                    memset(dstPx, srcPx[0], channels);
                }
            }
            else {
                // If the sticker IS see-through, calculate a blend of the two colors
                float outAlpha = srcAlpha + dstAlpha * (1 - srcAlpha);
                if(outAlpha < .01) {
                    memset(dstPx, 0, channels); // If both are invisible, set to 0
                } else {
                    for(int chnl = 0; chnl < channels; ++chnl) {
                        // Formula to mix the sticker color and background color based on transparency
                        dstPx[chnl] = (uint8_t)BYTE_BOUND((srcPx[chnl]/255.f * srcAlpha + dstPx[chnl]/255.f * dstAlpha * (1 - srcAlpha)) / outAlpha * 255.f);
                    }
                    if(channels > 3) {dstPx[3] = (uint8_t)BYTE_BOUND(outAlpha * 255.f);}
                }
            }
        }
    }
    return *this;
}

/**
 * OVERLAY TEXT
 * This draws letters on the image. It looks up each character in a Font file.
 */
Image& Image::overlayText(const char* txt, const Font& font, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    size_t len = strlen(txt); // Count how many letters are in your text
    SFT_Char c;
    uint8_t color[4] = {r, g, b, a}; // The color you want the text to be

    for(size_t i = 0; i < len; ++i) {
        // Ask the font library for the "picture" of the current letter
        if(sft_char(&font.sft, txt[i], &c) != 0) {
            printf("[ERROR] Missing character '%c'\n", txt[i]);
            continue;
        }

        // Draw the letter pixel by pixel
        for(uint16_t sy = 0; sy < c.height; ++sy) {
            int dy = sy + y + c.y;
            if(dy < 0 || dy >= h) {continue;}
            for(uint16_t sx = 0; sx < c.width; ++sx) {
                int dx = sx + x + c.x;
                if(dx < 0 || dx >= w) {continue;}
                
                uint8_t* dstPx = &data[(dx + dy * w) * channels];
                uint8_t srcPx = c.image[sx + sy * c.width];

                if(srcPx != 0) { // If this part of the letter is not empty
                    // Mix the letter color with the background color (Alpha Blending)
                    float srcAlpha = (srcPx / 255.f) * (a / 255.f);
                    float dstAlpha = channels < 4 ? 1 : dstPx[3] / 255.f;
                    float outAlpha = srcAlpha + dstAlpha * (1 - srcAlpha);
                    
                    for(int chnl = 0; chnl < channels; ++chnl) {
                        dstPx[chnl] = (uint8_t)BYTE_BOUND((color[chnl]/255.f * srcAlpha + dstPx[chnl]/255.f * dstAlpha * (1 - srcAlpha)) / outAlpha * 255.f);
                    }
                    if(channels > 3) {dstPx[3] = (uint8_t)BYTE_BOUND(outAlpha * 255.f);}
                }
            }
        }

        x += c.advance; // Move the "cursor" to the right for the next letter
        free(c.image); // Clean up the letter's temporary picture
    }
    return *this;
}








/**
 * CROP
 * This "cuts out" a specific rectangle from your image and throws the rest away.
 */
Image& Image::crop(uint16_t cx, uint16_t cy, uint16_t cw, uint16_t ch) {
    // Calculate the size of the new, smaller image
    size = cw * ch * channels;
    // Create a new blank memory space for the cropped piece
    uint8_t* croppedImage = new uint8_t[size];
    memset(croppedImage, 0, size); // Fill it with black initially

    // Loop through the new dimensions
    for(uint16_t y = 0; y < ch; ++y) {
        if(y + cy >= h) {break;} // Stop if we go past the original image height
        for(uint16_t x = 0; x < cw; ++x) {
            if(x + cx >= w) {break;} // Stop if we go past the original image width
            // Copy the pixel from the original "cut" area into our new small image
            memcpy(&croppedImage[(x + y * cw) * channels], &data[(x + cx + (y + cy) * w) * channels], channels);
        }
    }

    // Update the image details to the new size
    w = cw;
    h = ch;
    
    // Delete the old full-size image memory and use the new cropped memory
    delete[] data;
    data = croppedImage;
    croppedImage = nullptr;

    return *this;
}

/**
 * RESIZE (Nearest Neighbor)
 * This stretches or shrinks the image to a new size. 
 * It's called "Nearest Neighbor" because for every new pixel, it just finds the 
 * closest matching pixel from the original and copies it.
 */
Image& Image::resizeNN(uint16_t nw, uint16_t nh) {
    size = nw * nh * channels;
    uint8_t* newImage = new uint8_t[size];

    // Calculate how much to stretch/shrink in width (scaleX) and height (scaleY)
    float scaleX = (float)nw / (w);
    float scaleY = (float)nh / (h);

    for(uint16_t y = 0; y < nh; ++y) {
        // Find the "nearest" original row
        uint16_t sy = (uint16_t)(y / scaleY);
        for(uint16_t x = 0; x < nw; ++x) {
            // Find the "nearest" original column
            uint16_t sx = (uint16_t)(x / scaleX);

            // Copy that specific pixel into the new resized image
            memcpy(&newImage[(x + y * nw) * channels], &data[(sx + sy * w) * channels], channels);
        }
    }

    w = nw;
    h = nh;
    delete[] data;
    data = newImage;
    newImage = nullptr;

    return *this;
}
