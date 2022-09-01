/* File that contains the variable declarations */
#include "poseidon.h"
#include <stdio.h>

/* First function called. It initiallizes all the functions and variables used by AURORA */
void lib_init(int metric, int start_search)
{
	int i;
	int numCores = sysconf(_SC_NPROCESSORS_ONLN);
	/*Initialization of RAPL */
	//lib_detect_cpu();
	lib_detect_packages();
	/*End initialization of RAPL */

	int startThreads = numCores;
	while (startThreads != 2 && startThreads != 3 && startThreads != 5)
	{
		startThreads = startThreads / 2;
	}

	/* Initialization of the variables necessary to perform the search algorithm */
	for (i = 0; i < MAX_KERNEL; i++)
	{
		libKernels[i].numThreads = numCores;
		libKernels[i].startThreads = startThreads;
		libKernels[i].numCores = numCores;
		libKernels[i].initResult = 0.0;
		libKernels[i].state = REPEAT;
		libKernels[i].metric = metric;
		libKernels[i].hasSequentialBase = SEQUENTIAL_BASE_NOT_TESTED;
		idKernels[i] = 0;
	}

	/* Start the counters for energy and time for all the application execution */
	id_actual_region = MAX_KERNEL - 1;
	lib_start_amd_msr();
	initGlobalTime = omp_get_wtime();
}

/* It defines the number of threads that will execute the actual parallel region based on the current state of the search algorithm */
int lib_resolve_num_threads(uintptr_t ptr_region)
{
	int i;
	id_actual_region = -1;

	/* Find the actual parallel region */
	for (i = 0; i < totalKernels; i++)
	{
		if (idKernels[i] == ptr_region)
		{
			id_actual_region = i;
			break;
		}
	}

	/* If a new parallel region is discovered */
	if (id_actual_region == -1)
	{
		idKernels[totalKernels] = ptr_region;
		id_actual_region = totalKernels;
		totalKernels++;
	}

	/* Check the state of the search algorithm. */
	switch (libKernels[id_actual_region].state)
	{
	case END:
		return libKernels[id_actual_region].bestThread;
	default:
		lib_start_amd_msr();
		libKernels[id_actual_region].initResult = omp_get_wtime();
		return libKernels[id_actual_region].numThreads;
	}
}

// lib_end_amd_msr();
/* It is responsible for performing the search algorithm */
void lib_end_parallel_region()
{
	double time, energy, result = 0;
        if (libKernels[id_actual_region].state != END)
        {
                /* Check the metric that is being evaluated and collect the results */
                switch (libKernels[id_actual_region].metric)
                {
                case PERFORMANCE:
                        time = omp_get_wtime();
                        result = time - libKernels[id_actual_region].initResult;
                        break;
                case EDP:
                        time = omp_get_wtime() - libKernels[id_actual_region].initResult;
                        energy = lib_end_amd_msr();
                        result = time * energy;
                        /* If the result is negative, it means some problem while reading of the hardware counter. 
                        Then, the metric changes to performance */
                        if (result == 0.00000 || result < 0)
                        {
                                libKernels[id_actual_region].state = REPEAT;
                                libKernels[id_actual_region].metric = PERFORMANCE;
                        }
                        break;
                case POWER:
                        time = omp_get_wtime() - libKernels[id_actual_region].initResult;
                        energy = lib_end_amd_msr();
                        result = energy/time;
                        if (result == 0.00000 || result < 0)
                        {
                                libKernels[id_actual_region].state = REPEAT;
                                libKernels[id_actual_region].metric = PERFORMANCE;
                        }
                        break;
                case TEMPERATURE:
                        time = omp_get_wtime() - libKernels[id_actual_region].initResult;
                        energy = lib_end_amd_msr();
                        result = sqrt((energy*energy) + (time*time));
                        if (result == 0.00000 || result < 0)
                        {
                                libKernels[id_actual_region].state = REPEAT;
                                libKernels[id_actual_region].metric = PERFORMANCE;
                        }
                        break;
                }
                switch (libKernels[id_actual_region].state)
                {
                case REPEAT:
                        libKernels[id_actual_region].state = S0;
                        libKernels[id_actual_region].numThreads = libKernels[id_actual_region].startThreads;
                        libKernels[id_actual_region].lastThread = libKernels[id_actual_region].numThreads;
                        //printf("REPEAT - Região %d, Num Thread %d, Best Thread %d, Resultado Atual %lf, Melhor Resultado %lf\n", id_actual_region,
                        // libKernels[id_actual_region].numThreads, libKernels[id_actual_region].bestThread, result, libKernels[id_actual_region].bestResult);
                        break;
                case S0:
                        libKernels[id_actual_region].bestResult = result;
                        libKernels[id_actual_region].bestThread = libKernels[id_actual_region].numThreads;
                        libKernels[id_actual_region].numThreads = libKernels[id_actual_region].bestThread * 2;
                        libKernels[id_actual_region].state = S1;
                        //printf("S0 - Região %d, Num Thread %d, Best Thread %d, Resultado Atual %lf, Melhor Resultado %lf\n", id_actual_region,
                        // libKernels[id_actual_region].numThreads, libKernels[id_actual_region].bestThread, result, libKernels[id_actual_region].bestResult);
                        break;
                case S1:
                        if (result < libKernels[id_actual_region].bestResult)
                        { //comparing S0 to REPEAT
                                libKernels[id_actual_region].bestResult = result;
                                libKernels[id_actual_region].bestThread = libKernels[id_actual_region].numThreads;
                                if (libKernels[id_actual_region].hasSequentialBase == SEQUENTIAL_BASE_NOT_TESTED)
                                {
                                        /* if there are opportunities for improvements, then double the number of threads */
                                        if (libKernels[id_actual_region].numThreads * 2 <= libKernels[id_actual_region].numCores)
                                        {
                                                libKernels[id_actual_region].lastThread = libKernels[id_actual_region].numThreads;
                                                libKernels[id_actual_region].numThreads = libKernels[id_actual_region].bestThread * 2;
                                                libKernels[id_actual_region].state = S1;
                                        }
                                        else
                                        {
                                                /* It means that the best number so far is equal to the number of cores */
                                                /* Then, it will realize a guided search near to this number */
                                                libKernels[id_actual_region].pass = libKernels[id_actual_region].lastThread / 2;
                                                libKernels[id_actual_region].numThreads = libKernels[id_actual_region].numThreads - libKernels[id_actual_region].pass;
                                                if (libKernels[id_actual_region].pass == 1)
                                                {
                                                        libKernels[id_actual_region].state = S3;
                                                }
                                                else
                                                {
                                                        libKernels[id_actual_region].state = S2;
                                                }
                                        }
                                }
                                else
                                {
                                        libKernels[id_actual_region].state = END;
                                }
                        }
                        else
                        {
                                /* Thread scalability stopped */
                                /* Find the interval of threads that provided this result. */
                                /* if the best number of threads so far is equal to the number of cores, then go to.. */
                                if (libKernels[id_actual_region].numThreads == libKernels[id_actual_region].startThreads * 2 && libKernels[id_actual_region].hasSequentialBase == SEQUENTIAL_BASE_NOT_TESTED)
				{
					libKernels[id_actual_region].lastThread = libKernels[id_actual_region].numThreads;
					libKernels[id_actual_region].numThreads = 1;
					libKernels[id_actual_region].hasSequentialBase = SEQUENTIAL_BASE_TESTED;
				}

                                else if (libKernels[id_actual_region].bestThread == libKernels[id_actual_region].numCores / 2)
                                {
                                        libKernels[id_actual_region].pass = libKernels[id_actual_region].lastThread / 2;
                                        libKernels[id_actual_region].numThreads = libKernels[id_actual_region].numThreads - libKernels[id_actual_region].pass;
                                        if (libKernels[id_actual_region].pass == 1)
                                        {
                                                libKernels[id_actual_region].state = S3;
                                        }
                                        else
                                        {
                                                libKernels[id_actual_region].state = S2;
                                        }
                                }
                                else
                                {
                                        libKernels[id_actual_region].pass = libKernels[id_actual_region].lastThread / 2;
                                        libKernels[id_actual_region].numThreads = libKernels[id_actual_region].numThreads + libKernels[id_actual_region].pass;
                                        if (libKernels[id_actual_region].pass == 1)
                                        {
                                                libKernels[id_actual_region].state = S3;
                                        }
                                        else
                                        {
                                                libKernels[id_actual_region].state = S2;
                                        }
                                }
                        }
                        //printf("S1 - Região %d, Num Thread %d, Best Thread %d, Resultado Atual %lf, Melhor Resultado %lf\n", id_actual_region,
                        // libKernels[id_actual_region].numThreads, libKernels[id_actual_region].bestThread, result, libKernels[id_actual_region].bestResult);
                        break;
                case S2:
                        if (libKernels[id_actual_region].bestResult < result)
                        {
                                libKernels[id_actual_region].pass = libKernels[id_actual_region].pass / 2;
                                libKernels[id_actual_region].numThreads = libKernels[id_actual_region].numThreads + libKernels[id_actual_region].pass;
                                if (libKernels[id_actual_region].pass == 1)
                                {
                                        libKernels[id_actual_region].state = S3;
                                }
                                else
                                {
                                        libKernels[id_actual_region].state = S2;
                                }
                        }
                        else
                        {
                                libKernels[id_actual_region].bestThread = libKernels[id_actual_region].numThreads;
                                libKernels[id_actual_region].bestResult = result;
                                libKernels[id_actual_region].pass = libKernels[id_actual_region].pass / 2;
                                libKernels[id_actual_region].numThreads = libKernels[id_actual_region].numThreads + libKernels[id_actual_region].pass;
                                if (libKernels[id_actual_region].pass == 1)
                                {
                                        libKernels[id_actual_region].state = S3;
                                }
                                else
                                {
                                        libKernels[id_actual_region].state = S2;
                                }
                        }
                        //printf("S2 - Região %d, Num Thread %d, Best Thread %d, Resultado Atual %lf, Melhor Resultado %lf\n", id_actual_region,
                        // libKernels[id_actual_region].numThreads, libKernels[id_actual_region].bestThread, result, libKernels[id_actual_region].bestResult);
                        break;
                case S3: /*The last comparison to define the best number of threads*/
                        libKernels[id_actual_region].state = END;
                        if (result < libKernels[id_actual_region].bestResult)
                        {
                                libKernels[id_actual_region].bestThread = libKernels[id_actual_region].numThreads;
                        }
                        //printf("S3 - Região %d, Num Thread %d, Best Thread %d, Resultado Atual %lf, Melhor Resultado %lf\n", id_actual_region,
                        // libKernels[id_actual_region].numThreads, libKernels[id_actual_region].bestThread, result, libKernels[id_actual_region].bestResult);
                        break;
                }
        }
}

/* It finalizes the environment of Aurora */
void lib_destructor()
{
	double time = omp_get_wtime() - initGlobalTime;
	id_actual_region = MAX_KERNEL - 1;
	double energy = lib_end_amd_msr();
	float edp = time * energy;
	printf("POSEIDON - Execution Time: %.5f seconds\n", time);
	printf("POSEIDON - Energy: %.5f joules\n", energy);
	printf("POSEIDON - EDP: %.5f\n", edp);
}

void lib_detect_packages()
{

	char filename[STRING_BUFFER];
	FILE *fff;
	int package;
	int i;

	for (i = 0; i < MAX_PACKAGES; i++)
		package_map[i] = -1;

	for (i = 0; i < MAX_CPUS; i++)
	{
		sprintf(filename, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i);
		fff = fopen(filename, "r");
		if (fff == NULL)
			break;
		fscanf(fff, "%d", &package);
		fclose(fff);

		if (package_map[package] == -1)
		{
			libTotalPackages++;
			package_map[package] = i;
		}
	}
}

void lib_start_amd_msr()
{
	char msr_filename[STRING_BUFFER];
	int fd;
	sprintf(msr_filename, "/dev/cpu/0/msr");
	fd = open(msr_filename, O_RDONLY);
	if (fd < 0)
	{
		if (errno == ENXIO)
		{
			fprintf(stderr, "rdmsr: No CPU 0\n");
			exit(2);
		}
		else if (errno == EIO)
		{
			fprintf(stderr, "rdmsr: CPU 0 doesn't support MSRs\n");
			exit(3);
		}
		else
		{
			perror("rdmsr:open");
			fprintf(stderr, "Trying to open %s\n", msr_filename);
			exit(127);
		}
	}
	uint64_t data;
	pread(fd, &data, sizeof data, AMD_MSR_PACKAGE_ENERGY);
	libKernels[id_actual_region].kernelBefore[0] = (long long)data;
        close(fd);
}

double lib_end_amd_msr()
{
	char msr_filename[STRING_BUFFER];
	int fd;
	sprintf(msr_filename, "/dev/cpu/0/msr");
	fd = open(msr_filename, O_RDONLY);
	uint64_t data;
	pread(fd, &data, sizeof data, AMD_MSR_PWR_UNIT);
	int core_energy_units = (long long)data;
	unsigned int energy_unit = (core_energy_units & AMD_ENERGY_UNIT_MASK) >> 8;
	pread(fd, &data, sizeof data, AMD_MSR_PACKAGE_ENERGY);
	libKernels[id_actual_region].kernelAfter[0] = (long long)data;
	double result = (libKernels[id_actual_region].kernelAfter[0] - libKernels[id_actual_region].kernelBefore[0]) * pow(0.5, (float)(energy_unit));
	close(fd);
        return result;
}
