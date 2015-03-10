#include"io.h"
#include"utils.h"
#include"detector.h"
#include<string.h>
#include<math.h>
#include<stdio.h>
#include<stdlib.h>

int main()
{
    configuration *config;
    if ((config = malloc(sizeof(configuration)))==NULL)
    {
        printf("Cannot allocate config structure\n");
        abort();
    }
    read_config(config);

    FILE *input;
    if ((input = fopen64(config->filepath,"rb"))==NULL)
    {
        printf("Cannot open input file\n");
        abort();
    }

    uint64_t read;
    uint64_t pos;
    uint64_t start;
    uint64_t finish;
    uint64_t readlength;
    double binsize;
    double threshold;
    double hysteresis;
    //uint64_t order;
    double baseline_min;
    double baseline_max;
    uint64_t samplingfreq;
    double cusum_delta;
    double cusum_threshold;
    uint64_t maxpoints;
    uint64_t minpoints;
    int event_direction;
    int endflag;
    endflag = 0;
    read = 0;
    pos = 0;
    start = config->start;
    finish = config->finish;
    readlength = config->readlength;
    binsize = config->binsize;
    threshold = config->threshold;
    hysteresis = config->hysteresis;
    event_direction = config->event_direction;
    //order = config->poles;
    baseline_min = config->baseline_min;
    baseline_max = config->baseline_max;
    samplingfreq = config->samplingfreq;
    cusum_delta = config->cusum_delta;
    cusum_threshold = config->cusum_threshold;
    maxpoints = config->event_maxpoints;
    minpoints = config->event_minpoints;

    double *signal;
    if ((signal = (double *) calloc(readlength,sizeof(double)))==NULL)
    {
        printf("Cannot allocate signal array\n");
        abort();
    }

    histostruct *histogram;
    if ((histogram = malloc(sizeof(histostruct)))==NULL)
    {
        printf("cannot allocate histogram structure\n");
        abort();
    }
    histogram->histogram = NULL;
    histogram->numbins = 0;


    uint64_t i;
    i = 0;

    edge *head_edge;
    edge *current_edge;
    head_edge = initialize_edges();
    current_edge = head_edge;

    event *head_event;
    event *current_event;
    head_event = initialize_events();
    current_event = head_event;

    double baseline;

    printf("Locating events... ");
    for (pos = start; pos < finish; pos += read)
    {
        read = read_current(input, signal, pos, intmin(readlength,finish - pos));
        if (read < config->readlength || feof(input))
        {
            endflag = 1;
        }

        baseline = build_histogram(signal, histogram, read, binsize, baseline_max, baseline_min);

        if (baseline < baseline_min || baseline > baseline_max)
        {
            printf("Skipping section with bad baseline: %g\n", baseline);
        }
        else
        {
            current_edge = detect_edges(signal, baseline, read, current_edge, threshold, hysteresis, pos, event_direction);
        }

        if (endflag)
        {
            pos += read;
            break;
        }
        memset(signal,'0',(readlength)*sizeof(double));
    }
    printf("Finished\n");

    current_edge = head_edge;
    current_event = head_event;

    if (!current_edge || current_edge->type == HEAD)
    {
        printf("No edges found in signal, exiting\n");
        system("pause");
        return -1;
    }

    printf("Processing event locations... ");
    current_event = process_edges(current_edge, current_event);
    current_event = head_event;
    printf("Finished\n");


    if (!current_event || current_event->index == HEAD)
    {
        printf("No events found in signal, exiting\n");
        system("pause");
        return -2;
    }

    printf("Filtering on event length... ");
    filter_event_length(current_event, maxpoints, minpoints);
    head_event = delete_bad_events(head_event);
    current_event = head_event;
    printf("Finished\n");

    printf("Populating event traces... ");
    populate_event_traces(input, current_event);
    current_event = head_event;
    printf("Finished\n");

    printf("Assigning event baselines...");
    assign_event_baselines(current_event);
    current_event = head_event;
    printf("Finished\n");

    printf("Assigning event areas...");
    assign_event_areas(current_event, 1.0/samplingfreq);
    current_event = head_event;
    printf("Finished\n");

    printf("Detecting subevents...");
    detect_subevents(current_event, cusum_delta, cusum_threshold);
    current_event = head_event;
    printf("Finished\n");

    printf("Processing subevents...");
    assign_cusum_levels(current_event);
    current_event = head_event;
    printf("Finished\n");

    printf("Counting subevents...");
    count_all_levels(current_event);
    current_event = head_event;
    printf("Finished\n");

    printf("Printing all signals...");
    print_all_signals(current_event, 1.0/samplingfreq*1e6);
    current_event = head_event;
    printf("Finished\n");

    printf("Printing event summary...");
    print_events(current_event, 1.0/samplingfreq);
    current_event = head_event;
    printf("Finished\n");

    printf("Cleaning up memory usage...\n");
    free_events(head_event);
    printf("Event list freed...\n");
    free_edges(head_edge);
    printf("Edge list freed...\n");
    for (i=0; i<histogram->numbins; i++)
    {
        free(histogram->histogram[i]);
    }
    free(histogram->histogram);
    printf("Signal array freed\n");
    free(histogram);
    fclose(input);
    free(config);
    free(signal);
    printf("Finished\n");
    system("pause");
    return 0;
}
