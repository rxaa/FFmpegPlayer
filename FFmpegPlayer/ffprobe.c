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
#include "libavutil/timecode.h"
#include "libavutil/timestamp.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "cmdutils.h"

const char program_name[] = "ffplay";


THREAD_LOCAL_VAR char * json_buf = 0;
THREAD_LOCAL_VAR size_t buf_size = 32;
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
		free(json_buf);
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

static int nb_streams;
static uint64_t *nb_streams_packets;
static uint64_t *nb_streams_frames;
static int *selected_streams;

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
static int need_comma() {
	if (char_n > 0 && json_buf[char_n - 1] != '{' && json_buf[char_n - 1] != '[')
		return 1;
	return 0;
}
static void print_str(const char * key, const char * str) {

	if (need_comma())
		append_str(",");

	append_str("\"");
	append_str(key);
	append_str("\":\"");
	append_str(str);
	append_str("\"");

}

static void print_val(const char * key, int64_t ts, const char * val) {
	if (need_comma())
		append_str(",");

	append_str("\"");
	append_str(key);
	append_str("\":");


	if (ts < 0) {
		append_str("0");
	}
	else {
		char buf[128] = { 0 };
		struct unit_value uv;
		uv.val.i = ts;
		uv.unit = val;
		value_string(buf, sizeof(buf), uv);
		append_str(buf);
	}

}

static void print_q(const char *key, AVRational q, char sep) {
	AVBPrint buf;
	av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
	av_bprintf(&buf, "%d%c%d", q.num, sep, q.den);
	print_str(key, buf.str);
}

static void print_time(const char * key, int64_t ts) {

	if (need_comma())
		append_str(",");

	append_str("\"");
	append_str(key);
	append_str("\":");


	if (ts == AV_NOPTS_VALUE) {
		append_str("0");
	}
	else {
		char buf[128] = { 0 };
		/*double d = ts * av_q2d(AV_TIME_BASE_Q);
		struct unit_value uv;
		uv.val.d = d;
		uv.unit = unit_second_str;
		value_string(buf, sizeof(buf), uv);*/
		snprintf(buf, sizeof(buf), "%I64d", ts * 1000 / AV_TIME_BASE_Q.den);
		append_str(buf);
	}

}



static void print_uint(const char * key, uint64_t val) {

	if (need_comma())
		append_str(",");

	append_str("\"");
	append_str(key);
	append_str("\":");
	char buff[128] = { 0 };
	snprintf(buff, sizeof(buff), "%I64u", val);
	append_str(buff);
}


static void print_int(const char * key, int64_t val) {
	if (need_comma())
		append_str(",");

	append_str("\"");
	append_str(key);
	append_str("\":");
	char buff[128] = { 0 };
	snprintf(buff, sizeof(buff), "%I64d", val);
	append_str(buff);

}
static void print_ts(const char *key, int64_t ts)
{
	if (ts == AV_NOPTS_VALUE) {
		print_int(key, 0);
	}
	else {
		print_int(key, ts);
	}
}


static void print_color_range(enum AVColorRange color_range)
{
	const char *val = av_color_range_name(color_range);
	if (!val || color_range == AVCOL_RANGE_UNSPECIFIED) {
		//print_str_opt("color_range", "unknown");
	}
	else {
		print_str("color_range", val);
	}
}

static void print_color_space(enum AVColorSpace color_space)
{
	const char *val = av_color_space_name(color_space);
	if (!val || color_space == AVCOL_SPC_UNSPECIFIED) {
		//print_str_opt("color_space", "unknown");
	}
	else {
		print_str("color_space", val);
	}
}

static void print_primaries(enum AVColorPrimaries color_primaries)
{
	const char *val = av_color_primaries_name(color_primaries);
	if (!val || color_primaries == AVCOL_PRI_UNSPECIFIED) {
		//print_str_opt("color_primaries", "unknown");
	}
	else {
		print_str("color_primaries", val);
	}
}

static void print_color_trc(enum AVColorTransferCharacteristic color_trc)
{
	const char *val = av_color_transfer_name(color_trc);
	if (!val || color_trc == AVCOL_TRC_UNSPECIFIED) {
		//print_str_opt("color_transfer", "unknown");
	}
	else {
		print_str("color_transfer", val);
	}
}

static void print_chroma_location(enum AVChromaLocation chroma_location)
{
	const char *val = av_chroma_location_name(chroma_location);
	if (!val || chroma_location == AVCHROMA_LOC_UNSPECIFIED) {
		//print_str_opt("chroma_location", "unspecified");
	}
	else {
		print_str("chroma_location", val);
	}
}

#define print_fmt(k, f, ...) do {              \
    av_bprint_clear(&pbuf);                    \
    av_bprintf(&pbuf, f, __VA_ARGS__);         \
    print_str(k, pbuf.str);    \
} while (0)

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


static  int show_tags(AVDictionary *tags) {
	AVDictionaryEntry *tag = NULL;
	int ret = 0;

	if (!tags)
		return 0;

	append_str(",\"tags\":{");

	while ((tag = av_dict_get(tags, "", tag, AV_DICT_IGNORE_SUFFIX))) {
		print_str(tag->key, tag->value);
	}

	append_str("}");
	return ret;
}


static int show_format(InputFile *ifile)
{
	append_str("  \"format\":{");
	AVFormatContext *fmt_ctx = ifile->fmt_ctx;
	char val_str[128];
	int64_t size = fmt_ctx->pb ? avio_size(fmt_ctx->pb) : -1;
	int ret = 0;

	print_str("filename", fmt_ctx->url);
	print_uint("nb_streams", fmt_ctx->nb_streams);
	print_uint("nb_programs", fmt_ctx->nb_programs);
	print_str("format_name", fmt_ctx->iformat->name);
	if (fmt_ctx->iformat->long_name) print_str("format_long_name", fmt_ctx->iformat->long_name);

	print_time("start_time", fmt_ctx->start_time);
	print_time("duration", fmt_ctx->duration);
	if (size < 0)
		print_int("size", 0);
	else
		print_int("size", size);

	if (fmt_ctx->bit_rate < 0)
		print_int("bit_rate", 0);
	else
		print_int("bit_rate", fmt_ctx->bit_rate);

	print_int("probe_score", fmt_ctx->probe_score);
	ret = show_tags(fmt_ctx->metadata);



	append_str("  },\r\n");

	return ret;


}

static int show_stream(AVFormatContext *fmt_ctx, int stream_idx, InputStream *ist, int in_program)
{
	if (char_n > 0 && json_buf[char_n - 1] == '}')
		append_str(",");
	append_str("{");



	AVStream *stream = ist->st;
	AVCodecParameters *par;
	AVCodecContext *dec_ctx;
	char val_str[128];
	const char *s;
	AVRational sar, dar;
	AVBPrint pbuf;
	const AVCodecDescriptor *cd;
	int ret = 0;
	const char *profile = NULL;

	av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

	print_int("index", stream->index);

	par = stream->codecpar;
	dec_ctx = ist->dec_ctx;
	if (cd = avcodec_descriptor_get(par->codec_id)) {
		print_str("codec_name", cd->name);
		print_str("codec_long_name",
			cd->long_name ? cd->long_name : "unknown");
	}

	if ((profile = avcodec_profile_name(par->codec_id, par->profile)))
		print_str("profile", profile);
	else {
		if (par->profile != FF_PROFILE_UNKNOWN) {
			char profile_num[12];
			snprintf(profile_num, sizeof(profile_num), "%d", par->profile);
			print_str("profile", profile_num);
		}

	}

	s = av_get_media_type_string(par->codec_type);
	if (s) print_str("codec_type", s);

	if (dec_ctx)
		print_q("codec_time_base", dec_ctx->time_base, '/');
	print_str("codec_tag_string", av_fourcc2str(par->codec_tag));
	print_fmt("codec_tag", "0x%04"PRIx32, par->codec_tag);


	switch (par->codec_type) {
	case AVMEDIA_TYPE_VIDEO:
		print_int("width", par->width);
		print_int("height", par->height);
		print_int("coded_width", dec_ctx->coded_width);
		print_int("coded_height", dec_ctx->coded_height);

		print_int("has_b_frames", par->video_delay);
		sar = av_guess_sample_aspect_ratio(fmt_ctx, stream, NULL);
		if (sar.num) {
			print_q("sample_aspect_ratio", sar, ':');
			av_reduce(&dar.num, &dar.den,
				par->width  * sar.num,
				par->height * sar.den,
				1024 * 1024);
			print_q("display_aspect_ratio", dar, ':');
		}
		else {
			//print_str_opt("sample_aspect_ratio", "N/A");
			//print_str_opt("display_aspect_ratio", "N/A");
		}
		s = av_get_pix_fmt_name(par->format);
		if (s) print_str("pix_fmt", s);
		print_int("level", par->level);

		print_color_range(par->color_range);
		print_color_space(par->color_space);
		print_color_trc(par->color_trc);
		print_primaries(par->color_primaries);
		print_chroma_location(par->chroma_location);

		if (par->field_order == AV_FIELD_PROGRESSIVE)
			print_str("field_order", "progressive");
		else if (par->field_order == AV_FIELD_TT)
			print_str("field_order", "tt");
		else if (par->field_order == AV_FIELD_BB)
			print_str("field_order", "bb");
		else if (par->field_order == AV_FIELD_TB)
			print_str("field_order", "tb");
		else if (par->field_order == AV_FIELD_BT)
			print_str("field_order", "bt");
		/*else
			print_str_opt("field_order", "unknown");*/

		if (dec_ctx && dec_ctx->timecode_frame_start >= 0) {
			char tcbuf[AV_TIMECODE_STR_SIZE];
			av_timecode_make_mpeg_tc_string(tcbuf, dec_ctx->timecode_frame_start);
			print_str("timecode", tcbuf);
		}
		else {
			//print_str_opt("timecode", "N/A");
		}
		print_int("refs", dec_ctx->refs);
		break;

	case AVMEDIA_TYPE_AUDIO:
		s = av_get_sample_fmt_name(par->format);
		if (s) print_str("sample_fmt", s);
		print_val("sample_rate", par->sample_rate, unit_hertz_str);
		print_int("channels", par->channels);

		if (par->channel_layout) {
			av_bprint_clear(&pbuf);
			av_bprint_channel_layout(&pbuf, par->channels, par->channel_layout);
			print_str("v", pbuf.str);
		}
		else {
			//print_str_opt("channel_layout", "unknown");
		}

		print_int("bits_per_sample", av_get_bits_per_sample(par->codec_id));
		break;

	case AVMEDIA_TYPE_SUBTITLE:
		if (par->width)
			print_int("width", par->width);
		/*else
			print_str_opt("width", "N/A");*/
		if (par->height)
			print_int("height", par->height);
		/*else
			print_str_opt("height", "N/A");*/
		break;
	}

	if (dec_ctx && dec_ctx->codec && dec_ctx->codec->priv_class && show_private_data) {
		const AVOption *opt = NULL;
		while (opt = av_opt_next(dec_ctx->priv_data, opt)) {
			uint8_t *str;
			if (opt->flags) continue;
			if (av_opt_get(dec_ctx->priv_data, opt->name, 0, &str) >= 0) {
				print_str(opt->name, str);
				av_free(str);
			}
		}
	}

	if (fmt_ctx->iformat->flags & AVFMT_SHOW_IDS) print_fmt("id", "0x%x", stream->id);

	print_q("r_frame_rate", stream->r_frame_rate, '/');
	print_q("avg_frame_rate", stream->avg_frame_rate, '/');
	print_q("time_base", stream->time_base, '/');
	print_ts("start_pts", stream->start_time);
	print_time("start_time", stream->start_time, &stream->time_base);
	print_ts("duration_ts", stream->duration);
	print_time("duration", stream->duration, &stream->time_base);
	if (par->bit_rate > 0)     print_val("bit_rate", par->bit_rate, unit_bit_per_second_str);

	if (stream->codec->rc_max_rate > 0) print_val("max_bit_rate", stream->codec->rc_max_rate, unit_bit_per_second_str);


	if (dec_ctx && dec_ctx->bits_per_raw_sample > 0) print_fmt("bits_per_raw_sample", "%d", dec_ctx->bits_per_raw_sample);
	//else                                             print_str_opt("bits_per_raw_sample", "N/A");
	if (stream->nb_frames) print_fmt("nb_frames", "%"PRId64, stream->nb_frames);
	//else                   print_str_opt("nb_frames", "N/A");


	/* Print disposition information */
#define PRINT_DISPOSITION(flagname, name) do {                                \
        print_int(name, !!(stream->disposition & AV_DISPOSITION_##flagname)); \
    } while (0)

	append_str(",  \"disposition\":{");
	PRINT_DISPOSITION(DEFAULT, "default");
	PRINT_DISPOSITION(DUB, "dub");
	PRINT_DISPOSITION(ORIGINAL, "original");
	PRINT_DISPOSITION(COMMENT, "comment");
	PRINT_DISPOSITION(LYRICS, "lyrics");
	PRINT_DISPOSITION(KARAOKE, "karaoke");
	PRINT_DISPOSITION(FORCED, "forced");
	PRINT_DISPOSITION(HEARING_IMPAIRED, "hearing_impaired");
	PRINT_DISPOSITION(VISUAL_IMPAIRED, "visual_impaired");
	PRINT_DISPOSITION(CLEAN_EFFECTS, "clean_effects");
	PRINT_DISPOSITION(ATTACHED_PIC, "attached_pic");
	PRINT_DISPOSITION(TIMED_THUMBNAILS, "timed_thumbnails");
	append_str("}");

	ret = show_tags(stream->metadata);


	append_str("}");
}

static int show_streams(InputFile *ifile)
{

	append_str("  \"streams\":[");
	AVFormatContext *fmt_ctx = ifile->fmt_ctx;
	int i, ret = 0;

	for (i = 0; i < ifile->nb_streams; i++)
		if (selected_streams[i]) {
			ret = show_stream(fmt_ctx, i, &ifile->streams[i], 0);
			if (ret < 0)
				break;
		}
	append_str("  ]\r\n");

	return ret;
}


EXPORT_API char * WINAPI ffprobe_file_info(const char * filename)
{
	InputFile ifile = { 0 };
	int nb_streams = 0;
	uint64_t *nb_streams_packets = 0;
	uint64_t *nb_streams_frames = 0;
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


	/*

	ret = show_chapters(wctx, &ifile);
	CHECK_END;*/

	ret = show_format(&ifile);
	CHECK_END;

	ret = show_streams(&ifile);
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