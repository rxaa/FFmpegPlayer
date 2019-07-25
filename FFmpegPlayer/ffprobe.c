/*
 * Copyright (c) 2007-2010 Stefano Sabatini
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

 /**
  * @file
  * simple media prober based on the FFmpeg libraries
  */

#include "stdafx.h"
#include "ffprobe.h"

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/bprint.h"
#include "cmdutils.h"

const char program_name[] = "ffplay";


THREAD_LOCAL_VAR char * json_buf = 0;
THREAD_LOCAL_VAR size_t buf_size = 2048;
THREAD_LOCAL_VAR size_t char_n = 0;

static void init_json_buf() {
	if (json_buf == 0) {
		json_buf = malloc(buf_size);
	}
	char_n = 0;
	json_buf[0] = 0;
}

static void append_str(const char * str) {
	if (str == 0)
		return;

	if (json_buf == 0) {
		json_buf = malloc(buf_size);
	}

	int len = strlen(str);

	if (char_n + len + 1 >= buf_size) {
		while (char_n + len + 1 >= buf_size) {
			buf_size *= 2;
		}

		char * new_buf = malloc(buf_size);
		memcpy(new_buf, json_buf, char_n);
		json_buf = new_buf;
	}

	memcpy(json_buf + char_n, str, len + 1);
	char_n += len;
}


struct unit_value {
	union { double d; long long int i; } val;
	const char *unit;
};

static const struct {
	double bin_val;
	double dec_val;
	const char *bin_str;
	const char *dec_str;
} si_prefixes[] = {
	{ 1.0, 1.0, "", "" },
	{ 1.024e3, 1e3, "Ki", "K" },
	{ 1.048576e6, 1e6, "Mi", "M" },
	{ 1.073741824e9, 1e9, "Gi", "G" },
	{ 1.099511627776e12, 1e12, "Ti", "T" },
	{ 1.125899906842624e15, 1e15, "Pi", "P" },
};

static const char unit_second_str[] = "s";
static const char unit_hertz_str[] = "Hz";
static const char unit_byte_str[] = "byte";
static const char unit_bit_per_second_str[] = "bit/s";

static int show_value_unit = 0;
static int use_value_prefix = 0;
static int use_byte_value_binary_prefix = 0;
static int use_value_sexagesimal_format = 0;
static int show_private_data = 1;

static char *value_string(char *buf, int buf_size, struct unit_value uv)
{
	double vald;
	long long int vali;
	int show_float = 0;

	if (uv.unit == unit_second_str) {
		vald = uv.val.d;
		show_float = 1;
	}
	else {
		vald = vali = uv.val.i;
	}

	if (uv.unit == unit_second_str && use_value_sexagesimal_format) {
		double secs;
		int hours, mins;
		secs = vald;
		mins = (int)secs / 60;
		secs = secs - mins * 60;
		hours = mins / 60;
		mins %= 60;
		snprintf(buf, buf_size, "%d:%02d:%09.6f", hours, mins, secs);
	}
	else {
		const char *prefix_string = "";

		if (use_value_prefix && vald > 1) {
			long long int index;

			if (uv.unit == unit_byte_str && use_byte_value_binary_prefix) {
				index = (long long int) (log2(vald)) / 10;
				index = av_clip(index, 0, FF_ARRAY_ELEMS(si_prefixes) - 1);
				vald /= si_prefixes[index].bin_val;
				prefix_string = si_prefixes[index].bin_str;
			}
			else {
				index = (long long int) (log10(vald)) / 3;
				index = av_clip(index, 0, FF_ARRAY_ELEMS(si_prefixes) - 1);
				vald /= si_prefixes[index].dec_val;
				prefix_string = si_prefixes[index].dec_str;
			}
			vali = vald;
		}

		if (show_float || (use_value_prefix && vald != (long long int)vald))
			snprintf(buf, buf_size, "%f", vald);
		else
			snprintf(buf, buf_size, "%lld", vali);
		av_strlcatf(buf, buf_size, "%s%s%s", *prefix_string || show_value_unit ? " " : "",
			prefix_string, show_value_unit ? uv.unit : "");
	}

	return buf;
}

static void print_time(const char * key, int64_t ts, int comma) {

	append_str("\"");
	append_str(key);
	append_str("\":\"");


	if (ts == AV_NOPTS_VALUE) {
		append_str("N/A");
	}
	else {
		char buf[128] = { 0 };
		double d = ts * av_q2d(AV_TIME_BASE_Q);
		struct unit_value uv;
		uv.val.d = d;
		uv.unit = unit_second_str;
		value_string(buf, sizeof(buf), uv);
		append_str(buf);
	}
	append_str("\"");
	if (comma)
		append_str(",");

}


static void print_int(const char * key, unsigned int val, int comma) {

	append_str("\"");
	append_str(key);
	append_str("\":");
	char buff[128] = { 0 };
	snprintf(buff, sizeof(buff), "%d", val);
	append_str(buff);
	if (comma)
		append_str(",");

}

static void print_str(const char * key, const char * str, int comma) {

	append_str("\"");
	append_str(key);
	append_str("\":\"");
	append_str(str);
	append_str("\"");
	if (comma)
		append_str(",");

}

typedef struct WriterContext WriterContext;

typedef struct Writer {
	const AVClass *priv_class;      ///< private class of the writer, if any
	int priv_size;                  ///< private size for the writer context
	const char *name;

	int(*init)  (WriterContext *wctx);
	void(*uninit)(WriterContext *wctx);

	void(*print_section_header)(WriterContext *wctx);
	void(*print_section_footer)(WriterContext *wctx);
	void(*print_integer)       (WriterContext *wctx, const char *, long long int);
	void(*print_rational)      (WriterContext *wctx, AVRational *q, char *sep);
	void(*print_string)        (WriterContext *wctx, const char *, const char *);
	int flags;                  ///< a combination or WRITER_FLAG_*
} Writer;

#define SECTION_MAX_NB_LEVELS 10

struct WriterContext {
	const AVClass *class;           ///< class of the writer
	const Writer *writer;           ///< the Writer of which this is an instance
	char *name;                     ///< name of this writer instance
	void *priv;                     ///< private data for use by the filter

	const struct section *sections; ///< array containing all sections
	int nb_sections;                ///< number of sections

	int level;                      ///< current level, starting from 0

									/** number of the item printed in the given section, starting from 0 */
	unsigned int nb_item[SECTION_MAX_NB_LEVELS];

	/** section per each level */
	const struct section *section[SECTION_MAX_NB_LEVELS];
	AVBPrint section_pbuf[SECTION_MAX_NB_LEVELS]; ///< generic print buffer dedicated to each section,
												  ///  used by various writers

	unsigned int nb_section_packet; ///< number of the packet section in case we are in "packets_and_frames" section
	unsigned int nb_section_frame;  ///< number of the frame  section in case we are in "packets_and_frames" section
	unsigned int nb_section_packet_frame; ///< nb_section_packet or nb_section_frame according if is_packets_and_frames

	int string_validation;
	char *string_validation_replacement;
	unsigned int string_validation_utf8_flags;
};

typedef struct InputStream {
	AVStream *st;

	AVCodecContext *dec_ctx;
} InputStream;

void print_error_tojson(const char *filename, int err)
{
	char errbuf[128];
	const char *errbuf_ptr = errbuf;

	if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
		errbuf_ptr = strerror(AVUNERROR(err));
	append_str("\"");
	append_str("error");
	append_str("\":\"");
	append_str(filename);
	append_str(": ");
	append_str(errbuf_ptr);
	append_str("\"");
}

typedef struct InputFile {
	AVFormatContext *fmt_ctx;

	InputStream *streams;
	int       nb_streams;
} InputFile;

static int find_stream_info = 1;
static AVInputFormat *iformat = NULL;

static int open_input_file(InputFile *ifile, const char *filename)
{
	int err, i;
	AVFormatContext *fmt_ctx = NULL;
	AVDictionaryEntry *t;
	int scan_all_pmts_set = 0;

	fmt_ctx = avformat_alloc_context();
	if (!fmt_ctx) {
		print_error_tojson(filename, AVERROR(ENOMEM));
		return -1;
	}

	if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
		av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
		scan_all_pmts_set = 1;
	}
	if ((err = avformat_open_input(&fmt_ctx, filename,
		iformat, &format_opts)) < 0) {
		print_error_tojson(filename, err);
		return err;
	}
	ifile->fmt_ctx = fmt_ctx;
	if (scan_all_pmts_set)
		av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
	if ((t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
		av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
		print_str("error", "Option not found", 0);
		return AVERROR_OPTION_NOT_FOUND;
	}

	if (find_stream_info) {
		AVDictionary **opts = setup_find_stream_info_opts(fmt_ctx, codec_opts);
		int orig_nb_streams = fmt_ctx->nb_streams;

		err = avformat_find_stream_info(fmt_ctx, opts);

		for (i = 0; i < orig_nb_streams; i++)
			av_dict_free(&opts[i]);
		av_freep(&opts);

		if (err < 0) {
			print_error_tojson(filename, err);
			return err;
		}
	}

	av_dump_format(fmt_ctx, 0, filename, 0);

	ifile->streams = av_mallocz_array(fmt_ctx->nb_streams,
		sizeof(*ifile->streams));
	if (!ifile->streams) {
		print_str("error", "no streams", 0);
		return -1;
	}
	ifile->nb_streams = fmt_ctx->nb_streams;

	/* bind a decoder to each input stream */
	for (i = 0; i < fmt_ctx->nb_streams; i++) {
		InputStream *ist = &ifile->streams[i];
		AVStream *stream = fmt_ctx->streams[i];
		AVCodec *codec;

		ist->st = stream;

		if (stream->codecpar->codec_id == AV_CODEC_ID_PROBE) {
			av_log(NULL, AV_LOG_WARNING,
				"Failed to probe codec for input stream %d\n",
				stream->index);
			continue;
		}

		codec = avcodec_find_decoder(stream->codecpar->codec_id);
		if (!codec) {
			av_log(NULL, AV_LOG_WARNING,
				"Unsupported codec with id %d for input stream %d\n",
				stream->codecpar->codec_id, stream->index);
			continue;
		}
		{
			AVDictionary *opts = filter_codec_opts(codec_opts, stream->codecpar->codec_id,
				fmt_ctx, stream, codec);

			ist->dec_ctx = avcodec_alloc_context3(codec);
			if (!ist->dec_ctx)
			{
				print_str("error", "avcodec_alloc_context failed", 0);
				return -1;
			}

			err = avcodec_parameters_to_context(ist->dec_ctx, stream->codecpar);
			if (err < 0) {
				print_str("error", "avcodec_parameters_to_context failed", 0);
				return -1;
			}

			//if (do_show_log) {
			//	// For loging it is needed to disable at least frame threads as otherwise
			//	// the log information would need to be reordered and matches up to contexts and frames
			//	// That is in fact possible but not trivial
			//	av_dict_set(&codec_opts, "threads", "1", 0);
			//}

			ist->dec_ctx->pkt_timebase = stream->time_base;
			ist->dec_ctx->framerate = stream->avg_frame_rate;
#if FF_API_LAVF_AVCTX
			ist->dec_ctx->coded_width = stream->codec->coded_width;
			ist->dec_ctx->coded_height = stream->codec->coded_height;
#endif

			if (avcodec_open2(ist->dec_ctx, codec, &opts) < 0) {
				av_log(NULL, AV_LOG_WARNING, "Could not open codec for input stream %d\n",
					stream->index);

				print_str("error", "Could not open codec for input stream", 0);
				return -1;
			}

			if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
				av_log(NULL, AV_LOG_ERROR, "Option %s for input stream %d not found\n",
					t->key, stream->index);
				print_str("error", "Option not found", 0);
				return AVERROR_OPTION_NOT_FOUND;
			}
		}
	}

	ifile->fmt_ctx = fmt_ctx;
	return 0;
}

static void close_input_file(InputFile *ifile)
{
	int i;

	/* close decoder for each stream */
	for (i = 0; i < ifile->nb_streams; i++)
		if (ifile->streams[i].st->codecpar->codec_id != AV_CODEC_ID_NONE)
			avcodec_free_context(&ifile->streams[i].dec_ctx);

	av_freep(&ifile->streams);
	ifile->nb_streams = 0;

	avformat_close_input(&ifile->fmt_ctx);
}

#define REALLOCZ_ARRAY_STREAM(ptr, cur_n, new_n)                        \
{                                                                       \
    ret = av_reallocp_array(&(ptr), (new_n), sizeof(*(ptr)));           \
    if (ret < 0)                                                        \
        goto end;                                                       \
    memset( (ptr) + (cur_n), 0, ((new_n) - (cur_n)) * sizeof(*(ptr)) ); \
}




static int show_format(InputFile *ifile)
{
	AVFormatContext *fmt_ctx = ifile->fmt_ctx;
	char val_str[128];
	int64_t size = fmt_ctx->pb ? avio_size(fmt_ctx->pb) : -1;
	int ret = 0;

	print_str("filename", fmt_ctx->url, 1);
	print_int("nb_streams", fmt_ctx->nb_streams, 1);
	print_int("nb_programs", fmt_ctx->nb_programs, 1);
	print_str("format_name", fmt_ctx->iformat->name, 1);
	if (fmt_ctx->iformat->long_name) print_str("format_long_name", fmt_ctx->iformat->long_name, 1);
	else                             print_str("format_long_name", "unknown", 1);

	print_time("start_time", fmt_ctx->start_time, 1);
	print_time("duration", fmt_ctx->duration, 1);

}



EXPORT_API char * WINAPI ffprobe_file_info(const char * filename)
{
	InputFile ifile = { 0 };
	int nb_streams = 0;
	uint64_t *nb_streams_packets = 0;
	uint64_t *nb_streams_frames = 0;
	int *selected_streams = 0;
	char *stream_specifier = 0;
	init_json_buf();

	append_str("{");


	int ret = open_input_file(&ifile, filename);
	if (ret < 0) {
		goto end;
	}

#define CHECK_END if (ret < 0) goto end
	nb_streams = ifile.fmt_ctx->nb_streams;
	REALLOCZ_ARRAY_STREAM(nb_streams_frames, 0, ifile.fmt_ctx->nb_streams);
	REALLOCZ_ARRAY_STREAM(nb_streams_packets, 0, ifile.fmt_ctx->nb_streams);
	REALLOCZ_ARRAY_STREAM(selected_streams, 0, ifile.fmt_ctx->nb_streams);

	for (int i = 0; i < ifile.fmt_ctx->nb_streams; i++) {
		if (stream_specifier) {
			ret = avformat_match_stream_specifier(ifile.fmt_ctx,
				ifile.fmt_ctx->streams[i],
				stream_specifier);
			CHECK_END;
		else
			selected_streams[i] = ret;
		ret = 0;
		}
		else {
			selected_streams[i] = 1;
		}
		if (!selected_streams[i])
			ifile.fmt_ctx->streams[i]->discard = AVDISCARD_ALL;
	}
	AVFormatContext *fmt_ctx = ifile.fmt_ctx;


	/*ret = show_streams(wctx, &ifile);
	CHECK_END;

	ret = show_chapters(wctx, &ifile);
	CHECK_END;*/

	ret = show_format(&ifile);
	CHECK_END;



end:
	if (ifile.fmt_ctx)
		close_input_file(&ifile);
	av_freep(&nb_streams_frames);
	av_freep(&nb_streams_packets);
	av_freep(&selected_streams);
	append_str("}");
	return json_buf;
}