#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libvirt/libvirt.h>

#define BIT_SET(a,b) ((a) |= (1ULL<<(b)))

int ONE_MILLION = 1000000;

int numOfDomains;
virDomainPtr* allDomains;
virConnectPtr hypervisor;
int numOfHostCpus;
double* timeMappings; // in milliseconds
int* vCpuMappings;
virVcpuInfoPtr* allVCpuInfos;

/***
STATUS 9/13/2020:
on 4 pCPUs test 5 seems like its good?
pins do change somewhat. check it.
**/
int main(int argc, char *argv[]) {

	if (argc < 2) {
		printf("Missing command line args.\n");
		exit(-1);
	}

	int sleepInterval = atoi(argv[1]);
	if (sleepInterval == 0) {
		printf("Invalid sleep interval passed to program.\n");
		printf("Sleep interval: %s\n", argv[1]);
		exit(-1);
	}
	printf("Sleep interval: %i\n", sleepInterval);

	while (1) {
		hypervisor = virConnectOpen(NULL);
		balanceCPU(hypervisor);
		for (int i = 0; i < numOfDomains; i++) virDomainFree(allDomains[i]);
		virConnectClose(hypervisor);
		sleep(sleepInterval);
	}
	int isAlive = virConnectIsAlive(hypervisor);
	return 0;
}

/** Find vCPU with smallest time on busiest pCPU **/
int determineVCpuToMove(virDomainPtr *domainToMove, int vCpuMappings[], double timeMappings[]) {
	// BUG: least utilized vCPU should NOT be moved if it has a dedicated pCPU!
	int candidatepCpu;
	int hostCpus[numOfHostCpus];
	for (int i = 0; i < numOfHostCpus; i++) hostCpus[i] = 0;
	virVcpuInfoPtr vCpu = calloc(1, sizeof(virVcpuInfoPtr));
	/** Can further optimize to choose the best pCPU to make room on. **/
	for (int i = 0; i < numOfDomains; i++) {
		virDomainGetVcpus(allDomains[i], vCpu, 1, NULL, 0);
		hostCpus[vCpu->cpu] = hostCpus[vCpu->cpu] + 1;
		vCpu = calloc(1, sizeof(virVcpuInfoPtr));
	}
	for (int i = 0; i < numOfHostCpus; i++ ) if (hostCpus[i] > 1) { candidatepCpu = i; break; }

	int smallestvCpu;
	virVcpuInfoPtr smallestVCpuInfo = calloc(1, sizeof(virVcpuInfoPtr));
	virDomainPtr smallestDomain;
	unsigned int smallestvCpuTime = INT_MAX;
	int busiestpCPU = -1;
	double busiestpCPUTime = 0;
	for (int i = 0; i < numOfHostCpus; i++) {
		if (timeMappings[i] > busiestpCPUTime) { busiestpCPUTime = timeMappings[i]; busiestpCPU = i; }
	}
	for (int i = 0; i < numOfDomains; i++) {
		virDomainGetVcpus(allDomains[i], smallestVCpuInfo, 1, NULL, 0);
		int time = (smallestVCpuInfo->cpuTime) / ONE_MILLION;
		if ( smallestVCpuInfo->cpu == busiestpCPU && time < smallestvCpuTime && vCpuMappings[smallestVCpuInfo->cpu] > 1) {
			smallestvCpuTime = time;
			smallestvCpu = smallestVCpuInfo->number;
			*domainToMove = allDomains[i];
		}
		smallestVCpuInfo = calloc(1, sizeof(virVcpuInfoPtr));
	}
	printf("\nMoving vCPU from domain %s off of pCPU %i...", virDomainGetName(*domainToMove), busiestpCPU);
	return smallestvCpu;
}

void pinvCpuTopCPU(int vCpu, int pCpu, virDomainPtr domain) {
	int maxInfo = 1;
	int mapLen = 1;
	unsigned char * cpuMap = calloc(maxInfo, mapLen);
	virVcpuInfoPtr vCpuInfo = calloc(1, sizeof(virVcpuInfoPtr));

	int vCpuRes = virDomainGetVcpus(domain, vCpuInfo, maxInfo, cpuMap, mapLen);
	int n = 8;
	cpuMap[0] = cpuMap[0] & 0;
	BIT_SET(cpuMap[0], pCpu);
	int res = virDomainPinVcpu(domain, vCpu, cpuMap, mapLen);
	printf("\nReturn code from virDomainPinVcpu: %i \n", res);
}

void balance() {
	int pCpuToPin = -1;
	printf("First attempting to find an empty pCPU to pin to...\n");
	for (int i = 0; i < numOfHostCpus; i++) {
		if (vCpuMappings[i] == 0) {
			pCpuToPin = i;
			break;
		}
	}

	if (pCpuToPin == -1) {
		printf("No empty pCPUs were available. Finding the least utilized one...\n");
		double min = DBL_MAX;
		int leastUtilizedpCpu = -1;
		for (int i = 0; i < numOfHostCpus; i++) {
			if (timeMappings[i] < min) { min = timeMappings[i]; leastUtilizedpCpu = i; }
		}
		printf("Least utilized pCPU: %i\n", leastUtilizedpCpu);
		pCpuToPin = leastUtilizedpCpu;
	}
	printf("\npCPU to pin a vCPU to: %i", pCpuToPin);
	printf("\nFinding which vCPU to pin...");

	virDomainPtr domainToMove; // OUT ARG
	int vCpuToMove = determineVCpuToMove(&domainToMove, vCpuMappings, timeMappings);

	pinvCpuTopCPU(vCpuToMove, pCpuToPin, domainToMove);
}

int balanceCPU(virConnectPtr hypervisor) {
	numOfDomains = virConnectNumOfDomains(hypervisor);
	printf("\nNumber of domains: %i", numOfDomains);
	allDomains = malloc(numOfDomains * sizeof(virDomainPtr));
	int res = virConnectListAllDomains(hypervisor, &allDomains, VIR_CONNECT_LIST_DOMAINS_ACTIVE);


	if (!isBalanced(hypervisor, numOfDomains)) balance();

	return 0;
}

int isBalanced(virConnectPtr hypervisor, int numOfDomains) {
	virVcpuInfoPtr vCpuInfo = malloc(sizeof(virVcpuInfo));
	allVCpuInfos = calloc(numOfDomains, numOfDomains * sizeof(virVcpuInfo));
	numOfHostCpus = virNodeGetCPUMap(hypervisor, NULL, NULL, 0);

	/** Variable timeMappings is a map of total vCPU CPU time for
		 each pCPU. The index of this array is the number assigned
		 to the pCPU. The contents of the array at each index are
		 the cumulative CPU time across all domains for that pCPU.
		 **/
	timeMappings = calloc(numOfHostCpus, numOfHostCpus * sizeof(double));

	/** Variable vCpuMappings is a map of total number of vCPUs
		 currently using each pCPU. The index of this array is
		 the number assigned to the pCPU. The contents of the array
		 at each index are the number of vCPUs executing on that pCPU.
		 **/
	vCpuMappings = calloc(numOfHostCpus, numOfHostCpus * sizeof(int));

	/** Populate the vCpuMappings, timeMappings, and allvCpuInfos data structures. **/
	for (int i = 0; i < numOfDomains; i++) {
		int vCpuRes = virDomainGetVcpus(allDomains[i], vCpuInfo, 3, NULL, 0);
		printf("\nDomain: %s, vCPU Number: %i, vCpu State: %i, vCpu CPU Time (ms): %llu, pCpu Number: %i\n", virDomainGetName(allDomains[i]), vCpuInfo->number, vCpuInfo->state, vCpuInfo->cpuTime / ONE_MILLION, vCpuInfo->cpu);
		timeMappings[vCpuInfo->cpu] = timeMappings[vCpuInfo->cpu] + (vCpuInfo->cpuTime / ONE_MILLION);
		vCpuMappings[vCpuInfo->cpu] = vCpuMappings[vCpuInfo->cpu] + 1;
		allVCpuInfos[i] = vCpuInfo;
	}

	/** Begin actual algorithm for determining if we are in a balanced state. **/


	//If ANY pCPU has more than one vCPU AND there are empty pCPUs, unbalanced.
	// If there are no empty CPUs, and a pCPU has > 1 vCPU, check time usage.
	int emptyCpus = 0;
	int balanced = 1;
	int pCpuWithMoreThanOnevCpu = 0;

	for (int i = 0; i < numOfHostCpus; i++) if (vCpuMappings[i] == 0) emptyCpus = 1;
	for (int i = 0; i < numOfHostCpus; i++) {
		if (vCpuMappings[i] > 1) pCpuWithMoreThanOnevCpu = 1;
		if (vCpuMappings[i] > 1 && emptyCpus == 1) balanced = 0;
	}
	if (balanced && emptyCpus == 0 && pCpuWithMoreThanOnevCpu == 1) {
		printf("\nThere are no empty pCPUs, and some pCPUs have more than one vCPU.\n");
		printf("\nFinding balance based on time usage...\n");
		unsigned int averageTime;
		// this shows that we're getting a pretty good estimate of pCPU usage here.
		for (int i = 0; i < numOfHostCpus; i++) printf("pCPU %i time usage: %f\n", i, timeMappings[i]);
		for (int i = 0; i < numOfHostCpus; i++) averageTime = averageTime + timeMappings[i];
		averageTime = averageTime / numOfHostCpus;
		printf("\nAverage time for pCPUs: %i", averageTime);
		double unbalancedPercent = 0.1;
		double threshold = averageTime + (averageTime * unbalancedPercent);
		for (int i = 0; i < numOfHostCpus; i++) if (timeMappings[i] > threshold) balanced = 0;
		printf("\nThreshold time value over which system is unbalanced: %f\n", threshold);
	}
	printf("\nisBalanced: %i\n", balanced);
	return balanced;
}
