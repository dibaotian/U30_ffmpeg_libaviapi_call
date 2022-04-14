/**
 * @file
 * API example for decoding and filtering in the Xilinx U30
 * @example test_decoder.c
 */

#define _XOPEN_SOURCE 600 /* for usleep */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>

//xilinx xrm api
#include <xmaplugin.h>
#include <xrm.h>
#include <xma.h>
#include <xvbm.h> 

const char *filter_descr = "xvbm_convert";
const char *video_dst_filename = "1080p.yuv";
const char *graphFile = "graphFile.txt";

static FILE *video_dst_file = NULL;

static int video_dst_linesize[4];
static uint8_t *video_dst_data[4] = {NULL};
static int video_dst_bufsize;
static int video_frame_count = 0;
static int video_stream_idx = -1;


static int width, height;
static enum AVPixelFormat pix_fmt;


static AVFormatContext *fmt_ctx;
static AVCodecContext *dec_ctx;
AVFilterContext *buffersink_ctx;
AVFilterContext *buffersrc_ctx;
AVFilterGraph *filter_graph;
static int video_stream_index = -1;
static int64_t last_pts = AV_NOPTS_VALUE;

static int open_input_file(const char *filename)
{
    int ret;
    AVCodec *dec;

    if ((ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    video_dst_file = fopen(video_dst_filename, "wb");
    if (!video_dst_file) {
        av_log(NULL, AV_LOG_ERROR, "Could not open destination file %s\n", video_dst_filename);
        return (-1);
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    /* select the video stream */
    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find a video stream in the input file\n");
        return ret;
    }
    video_stream_index = ret;

    dec = avcodec_find_decoder_by_name("mpsoc_vcu_h264");//xilinx decoder 

	if (dec == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "mpsoc_vcu_h264 codec not found\n");  
		ret = -1;       
	}
    // else{
	// 	av_log(NULL, AV_LOG_INFO, "find the mpsoc_vcu_h264 codec\n");
	// }


    /* create decoding context */
    dec_ctx = avcodec_alloc_context3(dec);
    // dec_ctx->pix_fmt = AV_PIX_FMT_NV12;
    if (!dec_ctx)
        return AVERROR(ENOMEM);

    // avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[video_stream_index]->codecpar);

    /* Copy codec parameters from input stream to output codec context */
    if ((ret = avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[video_stream_index]->codecpar)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to copy %s codec parameters to decoder context\n",
                av_get_media_type_string(AVMEDIA_TYPE_VIDEO));  
        return ret;
    }

    // video_dst_file = fopen(video_dst_filename, "wb");
    // if (!video_dst_file) {
    //     av_log(NULL, AV_LOG_ERROR, "Could not open destination file %s\n", video_dst_filename);  
    //     ret = -1;
    //     return ret;
    // }

    /* allocate image where the decoded image will be put */
    width = dec_ctx->width;
    height = dec_ctx->height;
    pix_fmt = dec_ctx->pix_fmt;
    ret = av_image_alloc(video_dst_data, video_dst_linesize,width, height, pix_fmt, 1);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate raw video buffer\n");  
        return ret;
    }

    /* init the video decoder */
    if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open video decoder\n");
        return ret;
    }

    return 0;
}

static int init_filters(const char *filters_descr)
{
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVRational time_base = fmt_ctx->streams[video_stream_index]->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
    // enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_XVBM_8, AV_PIX_FMT_XVBM_10, AV_PIX_FMT_NONE };

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
            time_base.num, time_base.den,
            dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);
    // printf(args);
    av_log(NULL, AV_LOG_INFO, args);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr,
                                    &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;


    //将图形转储为人类可读的字符串表示形式。
	char *graph_str = avfilter_graph_dump(filter_graph, NULL);
	FILE* graph = NULL;
	// fopen_s(&graphFile, "graphFile.txt", "w");
    graph = fopen(graphFile, "w");
	fprintf(graph, "%s", graph_str);
	av_free(graph_str);
    fclose(graph);

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

static void display_frame(const AVFrame *frame, AVRational time_base)
{
    int x, y;
    uint8_t *p0, *p;
    int64_t delay;

    if (frame->pts != AV_NOPTS_VALUE) {
        if (last_pts != AV_NOPTS_VALUE) {
            /* sleep roughly the right amount of time;
             * usleep is in microseconds, just like AV_TIME_BASE. */
            delay = av_rescale_q(frame->pts - last_pts,
                                 time_base, AV_TIME_BASE_Q);
            if (delay > 0 && delay < 1000000)
                usleep(delay);
        }
        last_pts = frame->pts;
    }

    /* Trivial ASCII grayscale display. */
    p0 = frame->data[0];
    puts("\033c");
    for (y = 0; y < frame->height; y++) {
        p = p0;
        for (x = 0; x < frame->width; x++)
            putchar(" .-+#"[*(p++) / 52]);
        putchar('\n');
        p0 += frame->linesize[0];
    }
    fflush(stdout);
}

//XRM setup
int xrm_setup(){

	printf("xrm_setup\n");

	int ret_xrtreserve;
	int xrm_reserve_id;
	int i=0;
	//////////////////////////// XRM SETUP////////////////////////
	//Initialization--->Resource Reservation-->Session Creation-->Runtime Processing-->Cleanup
    av_log (NULL, AV_LOG_INFO, "\n<<<<<<<==  FFmpeg xrm ===>>>>>>>>\n");
	
	//first stup the connection with the xrm deamon
    xrmContext *xrm_ctx = (xrmContext *)xrmCreateContext(XRM_API_VERSION_1); 
    if (xrm_ctx == NULL)
    {
       av_log(NULL, AV_LOG_ERROR, "create local XRM context failed\n");
       return -1;
    }else{
		av_log(NULL, AV_LOG_INFO, "create local XRM context succeed!\n");
	}

	//two ways to register resource
	// 1 Using the job slot reservation tool  
	// 2 directly assgin 

	// in this program default only one U30 in the system

	//Next step to call the  xma_initialize() to intialize the XMA library
    //get the the kernel parmater
    XmaXclbinParameter xclbin_param;
	// setenv("XRM_RESERVE_ID", "9", 0 );

    if (getenv("XRM_RESERVE_ID"))
    {
       //Query the XRM reserved device resource to use 
       xrmCuPoolResource query_transcode_cu_pool_res;        
       memset(&query_transcode_cu_pool_res, 0, sizeof(query_transcode_cu_pool_res));
       xrm_reserve_id = atoi(getenv("XRM_RESERVE_ID"));
       ret_xrtreserve = xrmReservationQuery(xrm_ctx, xrm_reserve_id, &query_transcode_cu_pool_res);  
       if (ret_xrtreserve != 0)
       {
		//   printf("%s\n", ret_xrtreserve);
          av_log(NULL, AV_LOG_ERROR, "xrm_reservation_query: fail to query allocated cu list\n");          
          return -1;
       }
       else
       {
          if (query_transcode_cu_pool_res.cuNum >= 0)
          {
             //XCLBIN configuration
             xclbin_param.device_id = query_transcode_cu_pool_res.cuResources[i].deviceId;
             xclbin_param.xclbin_name = query_transcode_cu_pool_res.cuResources[i].xclbinFileName; 

             av_log (NULL, AV_LOG_INFO, "------------------------------------------------------------\n\n");
             av_log (NULL, AV_LOG_INFO, "   xclbin_name :  %s\n", query_transcode_cu_pool_res.cuResources[i].xclbinFileName);
             av_log (NULL, AV_LOG_INFO, "   device_id   :  %d\n\n", query_transcode_cu_pool_res.cuResources[i].deviceId);
             av_log (NULL, AV_LOG_INFO, "   XRM ReserveId =%d \n", xrm_reserve_id);
             av_log (NULL, AV_LOG_INFO, "------------------------------------------------------------\n\n");
           
             /* Initialize the Xilinx Media Accelerator */
             if (xma_initialize(&xclbin_param, 1) != 0)
             {
                av_log(NULL, AV_LOG_ERROR, "XMA Initialization failed\n");
                return -1;
             }
          }
          else
          {
             //Given XRM_RESERVE_ID is not correct, falling back to XRM_DEVICE_ID flow
             unsetenv("XRM_RESERVE_ID");            
          } 
       }
    }else{

		av_log (NULL, AV_LOG_INFO, "Cannot get XRM_RESERVE_ID  \n\n");
	}

	if (!getenv("XRM_RESERVE_ID"))
    {
          if (!getenv("XRM_DEVICE_ID"))
             setenv("XRM_DEVICE_ID", "0" , 0); //set defualt device to 0

          //XCLBIN configuration
          xclbin_param.device_id = atoi(getenv("XRM_DEVICE_ID"));
          xclbin_param.xclbin_name = "/opt/xilinx/xcdr/xclbins/transcode.xclbin"; 

          av_log (NULL, AV_LOG_INFO, "------------------------------------------------------------\n");
          av_log (NULL, AV_LOG_INFO, "   xclbin_name :  %s\n", xclbin_param.xclbin_name);
          av_log (NULL, AV_LOG_INFO, "   device_id   :  %d \n", xclbin_param.device_id);
          av_log (NULL, AV_LOG_INFO, "------------------------------------------------------------\n");

          /* Initialize the Xilinx Media Accelerator */
          if (xma_initialize(&xclbin_param, 1) != 0)
          {
             av_log(NULL, AV_LOG_ERROR, "ERROR: XMA Initialization failed. Program exiting\n");
             return -1;
          }
    }


    //Destroy XRM context created for querry
    if (xrmDestroyContext(xrm_ctx) != XRM_SUCCESS)
       av_log(NULL, AV_LOG_ERROR, "XRM : Query destroy context failed\n");

	return 0;
}

static int output_video_frame(AVFrame *frame)
{
    av_log(NULL, AV_LOG_INFO, "output_video_frame\n"); 
    if (frame->width != width || frame->height != height ||
        frame->format != pix_fmt) {
        /* To handle this change, one could call av_image_alloc again and
         * decode the following frames into another rawvideo file. */

         av_log(NULL, AV_LOG_INFO, "receive decoder data ERROR\n"); 

        // av_log(NULL, AV_LOG_ERROR, "Error: Width, height and pixel format have to be "
        //         "constant in a rawvideo file, but the width, height or "
        //         "pixel format of the input video changed:\n"
        //         "old: width = %d, height = %d, format = %s\n"
        //         "new: width = %d, height = %d, format = %s\n",
        //         width, height, av_get_pix_fmt_name(pix_fmt),
        //         frame->width, frame->height,
        //         av_get_pix_fmt_name(frame->format));


        return -1;
    }

    av_log(NULL, AV_LOG_INFO, "video_frame n:%d coded_n:%d\n",
           video_frame_count++, frame->coded_picture_number); 

    /* copy decoded frame to destination buffer:
     * this is required since rawvideo expects non aligned data */
    av_image_copy(video_dst_data, video_dst_linesize,
                  (const uint8_t **)(frame->data), frame->linesize,
                  pix_fmt, width, height);

    /* write to rawvideo file */
    fwrite(video_dst_data[0], 1, video_dst_bufsize, video_dst_file);
    return 0;
}

int main(int argc, char **argv)
{
    int ret;
    AVPacket packet;
    AVFrame *frame;
    AVFrame *filt_frame;

     

    if (argc != 2) {
        fprintf(stderr, "Usage: %s file\n", argv[0]);
        exit(1);
    }

    av_log_set_level(AV_LOG_DEBUG);

    frame = av_frame_alloc();
    filt_frame = av_frame_alloc();
    if (!frame || !filt_frame) {
        perror("Could not allocate frame");
        exit(1);
    }

    if(ret = xrm_setup() < 0){
        perror("xrm_setup fail\n");
        goto end;
    }
       

    if ((ret = open_input_file(argv[1])) < 0){
        perror("open_input_file fail\n");
        goto end;
    }
        

    if ((ret = init_filters(filter_descr)) < 0){
        perror("Could not init_filters\n");
        goto end;
    }
        
    /* read all packets */
    while (1) {
        if ((ret = av_read_frame(fmt_ctx, &packet)) < 0)
            break;

        if (packet.stream_index == video_stream_index) {
            ret = avcodec_send_packet(dec_ctx, &packet);
            // av_buffersrc_write_frame(source, frame);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error while sending a packet to the decoder\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    av_log(NULL, AV_LOG_INFO, "AVERROR(EAGAIN) or AVERROR_EOF\n");
                    break;
                } else if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while receiving a frame from the decoder\n");
                    goto end;
                }

                frame->pts = frame->best_effort_timestamp;
               

                /* push the decoded frame into the filtergraph */
                if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
                    break;
                }

                /* pull filtered frames from the filtergraph */
                // while (1) {
                //     ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                //     if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                //         break;
                //     if (ret < 0)
                //         goto end;
                //     display_frame(filt_frame, buffersink_ctx->inputs[0]->time_base);
                //     av_frame_unref(filt_frame);
                // }
                output_video_frame(frame);
                av_frame_unref(frame);
            }
        }
        av_packet_unref(&packet);
    }
end:
    avfilter_graph_free(&filter_graph);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_frame_free(&filt_frame);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        exit(1);
    }

    exit(0);
}
