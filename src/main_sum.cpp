#include "cl/sum_cl.h"
#include "libgpu/context.h"
#include "libgpu/shared_device_buffer.h"
#include <libutils/fast_random.h>
#include <libutils/misc.h>
#include <libutils/timer.h>

template<typename T>
void raiseFail(const T &a, const T &b, std::string message, std::string filename, int line)
{
    if (a != b) {
        std::cerr << message << " But " << a << " != " << b << ", " << filename << ":" << line << std::endl;
        throw std::runtime_error(message);
    }
}

#define EXPECT_THE_SAME(a, b, message) raiseFail(a, b, message, __FILE__, __LINE__)


int main(int argc, char **argv)
{
    int benchmarkingIters = 10;

    unsigned int reference_sum = 0;
    unsigned int n = 100*1000*1000;
    std::vector<unsigned int> as(n, 0);
    FastRandom r(42);
    for (int i = 0; i < n; ++i) {
        as[i] = (unsigned int) r.next(0, std::numeric_limits<unsigned int>::max() / n);
        reference_sum += as[i];
    }

    {
        timer t;
        for (int iter = 0; iter < benchmarkingIters; ++iter) {
            unsigned int sum = 0;
            for (int i = 0; i < n; ++i) {
                sum += as[i];
            }
            EXPECT_THE_SAME(reference_sum, sum, "CPU result should be consistent!");
            t.nextLap();
        }
        std::cout << "CPU:     " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
        std::cout << "CPU:     " << (n/1000.0/1000.0) / t.lapAvg() << " millions/s" << std::endl;
    }

    {
        timer t;
        for (int iter = 0; iter < benchmarkingIters; ++iter) {
            unsigned int sum = 0;
            #pragma omp parallel for reduction(+:sum)
            for (int i = 0; i < n; ++i) {
                sum += as[i];
            }
            EXPECT_THE_SAME(reference_sum, sum, "CPU OpenMP result should be consistent!");
            t.nextLap();
        }
        std::cout << "CPU OMP: " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
        std::cout << "CPU OMP: " << (n/1000.0/1000.0) / t.lapAvg() << " millions/s" << std::endl;
    }

    {
        // TODO: implement on OpenCL
        // gpu::Device device = gpu::chooseGPUDevice(argc, argv);
    }

    gpu::Device device = gpu::chooseGPUDevice(argc, argv);
    gpu::Context context;
    context.init(device.device_id_opencl);
    context.activate();
    gpu::gpu_mem_32u as_buffer;
    gpu::gpu_mem_32u sum_buffer;

    as_buffer.resizeN(n);
    as_buffer.writeN(as.data(), n);

    sum_buffer.resizeN(1);

    unsigned int workGroupSize = 128;
    unsigned int global_work_size = (n + workGroupSize - 1) / workGroupSize * workGroupSize;
    unsigned int n_work_groups = global_work_size / workGroupSize;

    {
        ocl::Kernel kernel(sum_kernel, sum_kernel_length, "globalAtomSum");
        kernel.compile();


        timer t;
        unsigned int sum = 0;
        for (int i = 0; i < benchmarkingIters; ++i) {
            sum_buffer.writeN(&sum, 1);
            kernel.exec(gpu::WorkSize(workGroupSize, global_work_size),
                        as_buffer, n, sum_buffer);
            t.nextLap();
        }
        sum_buffer.readN(&sum, 1);
        EXPECT_THE_SAME(reference_sum, sum, "GPU result should be consistent!");

        std::cout << "GPU (Atomic): " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
        std::cout << "GPU (Atomic): " << (n/1000.0/1000.0) / t.lapAvg() << " millions/s" << std::endl;
    }

    {
        ocl::Kernel kernel(sum_kernel, sum_kernel_length, "BatchSum");
        kernel.compile();


        timer t;
        unsigned int sum = 0;
        for (int i = 0; i < benchmarkingIters; ++i) {
            sum_buffer.writeN(&sum, 1);
            kernel.exec(gpu::WorkSize(workGroupSize, global_work_size / 32),
                        as_buffer, n, sum_buffer);
            t.nextLap();
        }
        sum_buffer.readN(&sum, 1);
        EXPECT_THE_SAME(reference_sum, sum, "GPU result should be consistent!");

        std::cout << "GPU (BatchSum): " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
        std::cout << "GPU (BatchSum): " << (n/1000.0/1000.0) / t.lapAvg() << " millions/s" << std::endl;
    }

    {
        ocl::Kernel kernel(sum_kernel, sum_kernel_length, "BatchSumCoalesed");
        kernel.compile();


        timer t;
        unsigned int sum = 0;
        for (int i = 0; i < benchmarkingIters; ++i) {
            sum_buffer.writeN(&sum, 1);
            kernel.exec(gpu::WorkSize(workGroupSize, global_work_size / 32),
                        as_buffer, n, sum_buffer);
            t.nextLap();
        }
        sum_buffer.readN(&sum, 1);
        EXPECT_THE_SAME(reference_sum, sum, "GPU result should be consistent!");

        std::cout << "GPU (BatchSumCoalesed): " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
        std::cout << "GPU (BatchSumCoalesed): " << (n/1000.0/1000.0) / t.lapAvg() << " millions/s" << std::endl;
    }
    {
        ocl::Kernel kernel(sum_kernel, sum_kernel_length, "LocalMemSUm");
        kernel.compile();


        timer t;
        unsigned int sum = 0;
        for (int i = 0; i < benchmarkingIters; ++i) {
            sum_buffer.writeN(&sum, 1);
            kernel.exec(gpu::WorkSize(workGroupSize, global_work_size),
                        as_buffer, n, sum_buffer);
            t.nextLap();
        }
        sum_buffer.readN(&sum, 1);
        EXPECT_THE_SAME(reference_sum, sum, "GPU result should be consistent!");

        std::cout << "GPU (LocalMemSum): " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
        std::cout << "GPU (LocalMemSum): " << (n/1000.0/1000.0) / t.lapAvg() << " millions/s" << std::endl;
    }
    {
        ocl::Kernel kernel(sum_kernel, sum_kernel_length, "TreeSum");
        kernel.compile();


        timer t;
        unsigned int sum = 0;
        for (int i = 0; i < benchmarkingIters; ++i) {
            sum_buffer.writeN(&sum, 1);
            kernel.exec(gpu::WorkSize(workGroupSize, global_work_size),
                        as_buffer, n, sum_buffer);
            t.nextLap();
        }
        sum_buffer.readN(&sum, 1);
        EXPECT_THE_SAME(reference_sum, sum, "GPU result should be consistent!");

        std::cout << "GPU (TreeSum): " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
        std::cout << "GPU (TreeSum): " << (n/1000.0/1000.0) / t.lapAvg() << " millions/s" << std::endl;
    }
}
