
#include "MapReduceClient.h"
#include "MapReduceFramework.h"
#include "Barrier.h"
#include <iostream>
#include <pthread.h>
#include <atomic>
#include <algorithm>
#include <semaphore.h>


typedef struct ThreadContext;

typedef struct {
    const MapReduceClient& client;
    const InputVec& inputVec;
    OutputVec& outputVec;
    std::vector<IntermediateVec>* shuffled_vectors;
    pthread_t* threadsPool;
    ThreadContext* threadsContexts;
    Barrier* sort_barrier;
    Barrier* shuffle_barrier;
    pthread_mutex_t percentageMutex;
    pthread_mutex_t outputVecMutex;
    pthread_mutex_t waitMutex;
    JobState* jobState;
    int numberOfThreads;
    bool calledWait;
    std::atomic<int>* shuffled_elements_atomic_counter;
    std::atomic<int>* output_elements_atomic_counter;
    std::atomic<int>* input_elements_atomic_counter;
    std::atomic<int>* mapped_input_elements_atomic_counter;
} JobContext;

struct ThreadContext {
    int threadId;
    IntermediateVec intermediateVec;
    JobContext* jobContext;
};


void shuffle(ThreadContext* threadContext)
{
    int total_num_of_intermediate_pairs = 0;
    for (auto context_addr = threadContext->jobContext->threadsContexts;
        context_addr < threadContext->jobContext->threadsContexts + threadContext->jobContext->numberOfThreads;
        context_addr++)
    {
        total_num_of_intermediate_pairs += context_addr->intermediateVec.size();
    }
    int total_num_of_pushed_pairs = 0;
    while (true)
    {
        K2* greatest_key = nullptr;
        for (auto context_addr = threadContext->jobContext->threadsContexts;
            context_addr < threadContext->jobContext->threadsContexts + threadContext->jobContext->numberOfThreads;
            context_addr++)
        {
            if (context_addr->intermediateVec.empty()) {continue;}
            K2* last_key = context_addr->intermediateVec.back().first;
            // initialize greatest_key with first last_pair.
            if (greatest_key == nullptr) {greatest_key = last_key; continue;}
            if (*greatest_key < *last_key) {greatest_key = last_key;}
        }
        if (greatest_key == nullptr) {break;}   // no more keys to pop (all intermediate vectors are empty).
        IntermediateVec greatest_key_vec;
        for (auto context_addr = threadContext->jobContext->threadsContexts;
            context_addr < threadContext->jobContext->threadsContexts + threadContext->jobContext->numberOfThreads;
            context_addr++)
        {
            if (context_addr->intermediateVec.empty()) {continue;}
            IntermediatePair last_pair = context_addr->intermediateVec.back();
            K2* last_key = last_pair.first;
            while (!(*last_key < *greatest_key) && !(*greatest_key < *last_key))
            {
                context_addr->intermediateVec.pop_back();
                greatest_key_vec.push_back(last_pair);
                total_num_of_pushed_pairs += 1;
                pthread_mutex_lock(&threadContext->jobContext->percentageMutex);
                threadContext->jobContext->jobState->percentage =
                        ((float)(total_num_of_pushed_pairs) / ((float) total_num_of_intermediate_pairs)) * 100;
                pthread_mutex_unlock(&threadContext->jobContext->percentageMutex);
                if (context_addr->intermediateVec.empty()) { break;}
                last_pair = context_addr->intermediateVec.back();
                last_key = last_pair.first;
            }
        }
        threadContext->jobContext->shuffled_vectors->push_back(greatest_key_vec);
    }
}

void* mapReduceWrapper(void* tc){
    auto threadContext = (ThreadContext*)tc;

    // starting map stage.
    threadContext->jobContext->jobState->stage = MAP_STAGE;
    int current_input_element_index = (*(threadContext->jobContext->input_elements_atomic_counter))++;
    while (current_input_element_index < (threadContext->jobContext->inputVec.size())) {
        InputPair current_input_element_pair = threadContext->jobContext->inputVec[current_input_element_index];
        threadContext->jobContext->client.map(current_input_element_pair.first,
                                              current_input_element_pair.second, threadContext);

        (*(threadContext->jobContext->mapped_input_elements_atomic_counter))++;

        pthread_mutex_lock(&threadContext->jobContext->percentageMutex);
        threadContext->jobContext->jobState->percentage =
                ((float)*(threadContext->jobContext->mapped_input_elements_atomic_counter) /
                 (float)threadContext->jobContext->inputVec.size()) * 100;
        pthread_mutex_unlock(&threadContext->jobContext->percentageMutex);
        current_input_element_index = (*(threadContext->jobContext->input_elements_atomic_counter))++;
    }
    std::sort(threadContext->intermediateVec.begin(), threadContext->intermediateVec.end(),
              [](const IntermediatePair& lhs, const IntermediatePair& rhs){return *(lhs.first) < *(rhs.first);});

    threadContext->jobContext->sort_barrier->barrier();

    // starting shuffle stage.
    if (threadContext->threadId == 0)
    {
        pthread_mutex_lock(&threadContext->jobContext->percentageMutex);
        threadContext->jobContext->jobState->stage = SHUFFLE_STAGE;
        threadContext->jobContext->jobState->percentage = 0.0;
        pthread_mutex_unlock(&threadContext->jobContext->percentageMutex);
        shuffle(threadContext);
    }
    threadContext->jobContext->shuffle_barrier->barrier();

    // starting reduce stage.
    threadContext->jobContext->jobState->stage = REDUCE_STAGE;
    threadContext->jobContext->jobState->percentage = 0.0;
    int current_shuffled_vec_index = (*(threadContext->jobContext->shuffled_elements_atomic_counter))++;
    while (current_shuffled_vec_index < (threadContext->jobContext->shuffled_vectors->size())) {
        const IntermediateVec current_shuffled_element_vec =
                threadContext->jobContext->shuffled_vectors->at(current_shuffled_vec_index);
        threadContext->jobContext->client.reduce(&current_shuffled_element_vec, threadContext);
        pthread_mutex_lock(&threadContext->jobContext->percentageMutex);
        threadContext->jobContext->jobState->percentage =
                ((float)*(threadContext->jobContext->output_elements_atomic_counter) /
                 (float)threadContext->jobContext->shuffled_vectors->size()) * 100;
        pthread_mutex_unlock(&threadContext->jobContext->percentageMutex);
        current_shuffled_vec_index = (*(threadContext->jobContext->shuffled_elements_atomic_counter))++;
    }
    return nullptr;
}

void releaseJobHandleResources(JobHandle job)
{
    auto job_context = (JobContext*)job;
    delete job_context->jobState;
    delete job_context->sort_barrier;
    delete job_context->shuffle_barrier;
    delete job_context->input_elements_atomic_counter;
    delete [] job_context->threadsPool;
    delete [] job_context->threadsContexts;
    delete job_context->shuffled_vectors;
    delete job_context->shuffled_elements_atomic_counter;
    delete job_context->output_elements_atomic_counter;
    delete job_context->mapped_input_elements_atomic_counter;
    delete job_context;
}

void waitForJob(JobHandle job)
{
    auto job_context = (JobContext*)job;
    pthread_mutex_lock(&job_context->waitMutex);
    if (!(job_context->calledWait))
    {
        job_context->calledWait= true;
        for (int i = 0; i < job_context->numberOfThreads; ++i) {
            int join_res = pthread_join(job_context->threadsPool[i], nullptr);
            if (join_res < 0)
            {
                std::cerr << "system error: pthread_join failed\n";
                exit(1);
            }
        }
    }
    pthread_mutex_unlock(&job_context->waitMutex);
}

void getJobState(JobHandle job, JobState* state)
{
    auto job_context = (JobContext*)job;
    pthread_mutex_lock(&job_context->percentageMutex);
    state->percentage = job_context->jobState->percentage;
    state->stage = job_context->jobState->stage;
    pthread_mutex_unlock(&job_context->percentageMutex);
}

void closeJobHandle(JobHandle job)
{
    waitForJob(job);
    releaseJobHandleResources(job);
}

void emit2 (K2* key, V2* value, void* context)
{
    auto threadContext = (ThreadContext*)context;
    threadContext->intermediateVec.push_back(IntermediatePair(key, value));
}

void emit3 (K3* key, V3* value, void* context)
{
    auto threadContext = (ThreadContext*)context;
    pthread_mutex_lock(&threadContext->jobContext->outputVecMutex);
    threadContext->jobContext->outputVec.push_back(OutputPair(key, value));
    (*(threadContext->jobContext->output_elements_atomic_counter))++;
    pthread_mutex_unlock(&threadContext->jobContext->outputVecMutex);
}

JobHandle startMapReduceJob(const MapReduceClient& client,
                            const InputVec& inputVec, OutputVec& outputVec, int multiThreadLevel)
{
    auto job_context = new JobContext{client, inputVec, outputVec};

    job_context->numberOfThreads = multiThreadLevel;
    job_context->jobState = new JobState();
    job_context->sort_barrier = new Barrier(multiThreadLevel);
    job_context->shuffle_barrier = new Barrier(multiThreadLevel);
    job_context->jobState->stage = UNDEFINED_STAGE;
    job_context->jobState->percentage = 0.0;
    job_context->calledWait = false;
    job_context->threadsPool = new pthread_t[multiThreadLevel];
    job_context->threadsContexts = new ThreadContext[multiThreadLevel];
    job_context->shuffled_vectors = new std::vector<IntermediateVec>();
    job_context->input_elements_atomic_counter = new std::atomic<int>(0);
    job_context->output_elements_atomic_counter = new std::atomic<int>(0);
    job_context->shuffled_elements_atomic_counter = new std::atomic<int>(0);
    job_context->percentageMutex = PTHREAD_MUTEX_INITIALIZER;
    job_context->outputVecMutex = PTHREAD_MUTEX_INITIALIZER;
    job_context->waitMutex = PTHREAD_MUTEX_INITIALIZER;
    job_context->mapped_input_elements_atomic_counter = new std::atomic<int>(0);

    for (int i=0; i < multiThreadLevel; i++)
    {
        job_context->threadsContexts[i].threadId = i;
        job_context->threadsContexts[i].jobContext = job_context;
        int create_res = pthread_create(&job_context->threadsPool[i], NULL, mapReduceWrapper,
                                        &job_context->threadsContexts[i]);
        if (create_res < 0)
        {
            std::cerr << "system error: pthread_create failed\n";
            exit(1);
        }
    }
    return job_context;
}
