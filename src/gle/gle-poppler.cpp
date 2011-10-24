/************************************************************************
 *                                                                      *
 * GLE - Graphics Layout Engine <http://www.gle-graphics.org/>          *
 *                                                                      *
 * Modified BSD License                                                 *
 *                                                                      *
 * Copyright (C) 2009 GLE.                                              *
 *                                                                      *
 * Redistribution and use in source and binary forms, with or without   *
 * modification, are permitted provided that the following conditions   *
 * are met:                                                             *
 *                                                                      *
 *    1. Redistributions of source code must retain the above copyright *
 * notice, this list of conditions and the following disclaimer.        *
 *                                                                      *
 *    2. Redistributions in binary form must reproduce the above        *
 * copyright notice, this list of conditions and the following          *
 * disclaimer in the documentation and/or other materials provided with *
 * the distribution.                                                    *
 *                                                                      *
 *    3. The name of the author may not be used to endorse or promote   *
 * products derived from this software without specific prior written   *
 * permission.                                                          *
 *                                                                      *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR   *
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED       *
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE   *
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY       *
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL   *
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE    *
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS        *
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER *
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR      *
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN  *
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                        *
 *                                                                      *
 ************************************************************************/

#include "all.h"
#include "gle-poppler.h"
#include "cutils.h"
#include "gle-interface/gle-const.h"


#ifdef HAVE_POPPLER

#include <glib.h>
#include <cairo.h>
#include <poppler.h>

void gle_glib_init(int /* argc */, char** /* argv */) {
	g_type_init();
}

void gle_write_ostream(void* closure, char* data, int length) {
	std::ostream* file = (std::ostream*)closure;
	file->write(data, length);
}

struct GLEWriteFuncAndClosure {
	gle_write_func writeFunc;
	void* closure;
};

#ifdef HAVE_LIBPNG

#include <png.h>

void gle_png_write_data_fn(png_structp png_ptr, png_bytep data, png_size_t length) {
	GLEWriteFuncAndClosure* writeCallback = (GLEWriteFuncAndClosure*)png_get_io_ptr(png_ptr);
	writeCallback->writeFunc(writeCallback->closure, (char*)data, (int)length);
}

void gle_png_flush_fn(png_structp /* png_ptr */) {
}

void gle_write_cairo_surface_png(cairo_surface_t* surface,
		                         int options,
		                         gle_write_func writeFunc,
		                         void* closure) {
	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		CUtilsAssertMessage("png_create_write_struct failed");
	}
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		CUtilsAssertMessage("png_create_info_struct failed");
	}
	if (setjmp(png_jmpbuf(png_ptr))) {
		CUtilsAssertMessage("png_set_write_fn failed");
	}
	GLEWriteFuncAndClosure writeCallback;
	writeCallback.writeFunc = writeFunc;
	writeCallback.closure = closure;
	png_set_write_fn(png_ptr, (void*)&writeCallback, gle_png_write_data_fn, gle_png_flush_fn);
	if (setjmp(png_jmpbuf(png_ptr))) {
		CUtilsAssertMessage("png_set_IHDR failed");
	}
	int width = cairo_image_surface_get_width(surface);
	int height = cairo_image_surface_get_height(surface);
	int channels = 3;
	int color_type = PNG_COLOR_TYPE_RGB;
	if ((options & GLE_OUTPUT_OPTION_TRANSPARENT) != 0) {
		color_type = PNG_COLOR_TYPE_RGBA;
		channels = 4;
	}
	if ((options & GLE_OUTPUT_OPTION_GRAYSCALE) != 0) {
		color_type = PNG_COLOR_TYPE_GRAY;
		channels = 1;
	}
	png_set_IHDR(png_ptr, info_ptr, width, height,
				 8, color_type, PNG_INTERLACE_NONE,
				 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
	png_write_info(png_ptr, info_ptr);
	if (setjmp(png_jmpbuf(png_ptr))) {
		CUtilsAssertMessage("png_write_image failed");
	}
	int rowBytes = png_get_rowbytes(png_ptr, info_ptr);
	unsigned char* imageData = cairo_image_surface_get_data(surface);
	int stride = cairo_image_surface_get_stride(surface);
	CUtilsAssert(imageData != 0);
	png_bytep* row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
	for (int y = 0; y < height; y++) {
		png_byte* row = (png_byte*)malloc(rowBytes);
		row_pointers[y] = row;
		for (int x = 0; x < width; x++) {
			png_byte* ptr = &(row[x * channels]);
			unsigned int value = *(unsigned int*)(&imageData[x*4  + y*stride]);
			if (channels == 1) {
				int blue = value & 0xFF; /* blue */
				value >>= 8;
				int green = value & 0xFF; /* green */
				value >>= 8;
				int red = value & 0xFF; /* red */
				double gray = (3.0 * red / 255.0 + 2.0 * green / 255.0 + 1.0 * blue / 255.0) / 6.0 * 255.0;
				int gray_i = std::min<int>(gle_round_int(gray), 0xFF);
				*ptr = (png_byte)gray_i;
			} else {
				ptr[2] = value & 0xFF; /* blue */
				value >>= 8;
				ptr[1] = value & 0xFF; /* green */
				value >>= 8;
				ptr[0] = value & 0xFF; /* red */
				if (channels == 4) {
					value >>= 8;
					ptr[3] = value & 0xFF; /* alpha */
				}
			}
		}
	}
	png_write_image(png_ptr, row_pointers);
	if (setjmp(png_jmpbuf(png_ptr))) {
		CUtilsAssertMessage("png_write_end failed");
	}
	png_write_end(png_ptr, NULL);
	for (int y = 0; y < height; y++) {
		free(row_pointers[y]);
	}
	free(row_pointers);
}

#endif // HAVE_LIBPNG

#ifdef HAVE_LIBJPEG

#include <jpeglib.h>

#define GLE_JPEG_BUFFER_SIZE 10000

struct gle_jpeg_destination_mgr {
	struct jpeg_destination_mgr pub;
	JOCTET* buffer;
	GLEWriteFuncAndClosure writeCallback;
};

void gle_jpeg_memory_dest(j_compress_ptr cinfo, JOCTET* buffer, GLEWriteFuncAndClosure writeCallback) {
	if (cinfo->dest == 0) {
		cinfo->dest = (struct jpeg_destination_mgr*)(*cinfo->mem->alloc_small)((j_common_ptr) cinfo,
				                                                               JPOOL_PERMANENT,
	                                                                           sizeof(gle_jpeg_destination_mgr));
	}
	gle_jpeg_destination_mgr* dest = (gle_jpeg_destination_mgr*)cinfo->dest;
	dest->buffer = buffer;
	dest->writeCallback = writeCallback;
/*	dest->pub.init_destination = gle_jpeg_init_destination;
	dest->pub.empty_output_buffer = gle_jpeg_empty_output_buffer;
	dest->pub.term_destination = gle_jpeg_term_destination;  */
}

void gle_write_cairo_surface_jpeg(cairo_surface_t* surface,
		                          int options,
		                          gle_write_func writeFunc,
		                          void* closure)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);

}

#endif // HAVE_LIBJPEG

void gle_convert_pdf_to_image(char* pdfData,
		                      int pdfLength,
		                      double resolution,
		                      int device,
		                      int options,
		                      gle_write_func writeFunc,
		                      void* closure)
{
	GError* err = 0;
	PopplerDocument* doc = poppler_document_new_from_data(pdfData, pdfLength, 0, &err);
	if (doc == 0) {
		std::ostringstream errMsg;
		errMsg << ">> error opening PDF: " << err->message;
		g_object_unref(err);
		g_throw_parser_error(errMsg.str());
	}
	PopplerPage* page = poppler_document_get_page(doc, 0);
    if (page == 0) {
    	g_object_unref(doc);
    	g_throw_parser_error(">> error opening PDF: can't read first page");
    }
    double width, height;
    poppler_page_get_size(page, &width, &height);
    int i_width = gle_round_int(width / PS_POINTS_PER_INCH * resolution);
    int i_height = gle_round_int(height / PS_POINTS_PER_INCH * resolution);
    cairo_format_t format = CAIRO_FORMAT_RGB24;
	if ((options & GLE_OUTPUT_OPTION_TRANSPARENT) != 0 && device == GLE_DEVICE_PNG) {
		format = CAIRO_FORMAT_ARGB32;
	}
    cairo_surface_t* surface = cairo_image_surface_create(format, i_width, i_height);
    cairo_t* cr = cairo_create(surface);
    if (format == CAIRO_FORMAT_RGB24) {
    	cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
    	cairo_paint(cr);
    }
    cairo_scale(cr, resolution / PS_POINTS_PER_INCH, resolution / PS_POINTS_PER_INCH);
    poppler_page_render(page, cr);
#ifdef HAVE_LIBPNG
    if (device == GLE_DEVICE_PNG) {
    	gle_write_cairo_surface_png(surface, options, writeFunc, closure);
    }
#endif // HAVE_LIBPNG
#ifdef HAVE_LIBJPEG
    if (device == GLE_DEVICE_JPEG) {
    	gle_write_cairo_surface_jpeg(surface, options, writeFunc, closure);
    }
#endif // HAVE_LIBJPEG
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(page);
    g_object_unref(doc);
}

void gle_convert_pdf_to_image_file(char* pdfData,
		                           int pdfLength,
		                           double resolution,
		                           int device,
		                           int options,
		                           const char* fname)
{
	std::ofstream file(fname,  ios_base::out |  ios_base::binary);
	if (!file.is_open()) {
		g_throw_parser_error(">> error creating '", fname, "'");
	}
	gle_convert_pdf_to_image(pdfData,
			                 pdfLength,
			                 resolution,
			                 device,
			                 options,
			                 gle_write_ostream,
			                 static_cast<std::ostream*>(&file));
	file.close();
}

#else // HAVE_POPPLER

// stubs

void gle_glib_init(int /* argc */, char** /* argv */) {
}

#endif // HAVE_POPPLER
