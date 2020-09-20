#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libvirt/libvirt.h>

#define BIT_SET(a,b) ((a) |= (1ULL<<(b)))

#define BYTES_IN_KIBIBYTE 1024

#define DEFAULT_DEBUG_VALUE 69

/** (UnusedMemory / MaxMem) * 100, below this we are in danger of OOM**/
double MIN_MEM_THRESHOLD_GUEST = 8.0;
int PAGE_SIZE_IN_KB = 4;
int PAGES_TO_ALLOCATE_AT_ONCE = 15500;
const double MIN_MEM_AVAILABLE_ON_HOST = 0.05;

int numOfDomains;
virDomainPtr* allDomains;
virConnectPtr hypervisor;

int printMemoryStats() {
	printf("\nMemory Statistics:\n");
	printf("---------------------");
	for (int i = 0; i < numOfDomains; i++) {
		virDomainMemoryStatPtr stats = calloc(VIR_DOMAIN_MEMORY_STAT_NR, sizeof(virDomainMemoryStatStruct));
		printf("\nDomain: %s\n", virDomainGetName(allDomains[i]));
		int maxMem = virDomainGetMaxMemory(allDomains[i]);
		int numRet = virDomainMemoryStats(allDomains[i], stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);
		unsigned long long unused = DEFAULT_DEBUG_VALUE;
		unsigned long long available = DEFAULT_DEBUG_VALUE;
		unsigned long long balloonSize = DEFAULT_DEBUG_VALUE;
		unsigned long long usable = DEFAULT_DEBUG_VALUE;

		for (int j = 0; j < numRet; j++) {
			virDomainMemoryStatStruct s = stats[j];
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_UNUSED) unused = s.val;
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE) available = s.val;
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) balloonSize = s.val;
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_USABLE) usable = s.val;
		}
		int m = virDomainGetMaxMemory(allDomains[i]);
		printf("Value returned from virDomainGetMaxMemory: %fGB\n", m/(1024.0*1024));
		printf("Memory left unused by domain: %llukB\n", unused);
		//printf("Memory usable by domain:      %llukB\n", available);
		printf("Current balloon size:         %llukB\n", balloonSize);
	    printf("Memory actually available as a percent of maximum allowed: %f percent\n", ((float)unused / (float)(maxMem)) * (100));
	}
	printf("---------------------\n");
}

void giveMemory(virDomainPtr domain, unsigned long long balloonSize) {

	/** Before giveMemory is called, takeMemoryAway should have been called already.
	  * This means any VM that could give up memory already has, and the host has
	  * all of the memory it could get. **/
	const char* domainName = virDomainGetName(domain);
	virNodeInfoPtr nodeInfo = calloc(1, sizeof(virNodeInfo));
	virNodeGetInfo(hypervisor, nodeInfo);
    printf("Attempting to give domain %s %i kB\n", domainName, PAGE_SIZE_IN_KB * PAGES_TO_ALLOCATE_AT_ONCE);
	double hostFreeMem = virNodeGetFreeMemory(hypervisor) / (1024.0); // kB
	double hostTotalMem = nodeInfo->memory; //kB
	free(nodeInfo);
	if (hostFreeMem / hostTotalMem < MIN_MEM_AVAILABLE_ON_HOST) {
		printf("%f Gb host free memory. %f Gb host total memory.\n", hostFreeMem/(1024*1024), hostTotalMem/(1024*1024));
		printf("Host free memory: %f percent. Less than %f memory available to host OS. Cannot give a VM more memory.\n", hostFreeMem / hostTotalMem, MIN_MEM_AVAILABLE_ON_HOST);
		return;
	}

	if (virDomainGetMaxMemory(domain) <= (balloonSize + (PAGE_SIZE_IN_KB * PAGES_TO_ALLOCATE_AT_ONCE))) {
		printf("Domain %s cannot be given any more memory or it will exceed its maximum allowed.\n", domainName);
		return;
	}

	int res = virDomainSetMemoryFlags(domain, balloonSize + (PAGE_SIZE_IN_KB * PAGES_TO_ALLOCATE_AT_ONCE), VIR_DOMAIN_AFFECT_LIVE);
	if (res != 0) printf("virDomainSetMemory failed.\n");
	return;
}

void takeMemoryAway(virDomainPtr domain, unsigned long long balloonSize) {
	printf("Taking from domain %s %i kB\n", virDomainGetName(domain), PAGE_SIZE_IN_KB * PAGES_TO_ALLOCATE_AT_ONCE);
	int res = virDomainSetMemoryFlags(domain, balloonSize - (PAGE_SIZE_IN_KB * PAGES_TO_ALLOCATE_AT_ONCE), VIR_DOMAIN_AFFECT_LIVE);
	if (res != 0) printf("virDomainSetMemory failed.\n");
	return;
}

/** Loops through all domains. If a domain has too much memory, it takes some away
  * and gives it back to the host.
  * If a domain needs more memory, we try and give it some. */
int balanceMemory() {
	for (int i = 0; i < numOfDomains; i++) {
		virDomainMemoryStatPtr stats = calloc(VIR_DOMAIN_MEMORY_STAT_NR, sizeof(virDomainMemoryStatStruct));
		int maxMem = virDomainGetMaxMemory(allDomains[i]);
		int numRet = virDomainMemoryStats(allDomains[i], stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);
		unsigned long long unused = DEFAULT_DEBUG_VALUE;
		unsigned long long available = DEFAULT_DEBUG_VALUE;
		unsigned long long balloonSize = DEFAULT_DEBUG_VALUE;
		unsigned long long usable = DEFAULT_DEBUG_VALUE;

		for (int j = 0; j < numRet; j++) {
			virDomainMemoryStatStruct s = stats[j];
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_UNUSED) unused = s.val;
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE) available = s.val;
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) balloonSize = s.val;
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_USABLE) usable = s.val;
		}

		double r = ((float)unused / (float)(maxMem)) * (100);
		if ( r > (MIN_MEM_THRESHOLD_GUEST + 5)) takeMemoryAway(allDomains[i], balloonSize);
		if ( r < (MIN_MEM_THRESHOLD_GUEST)) giveMemory(allDomains[i], balloonSize);
	}
}

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
		hypervisor = virConnectOpen("qemu:///system");
		populateAllDomainsArray();
		for (int i = 0; i < numOfDomains; i++) virDomainSetMemoryStatsPeriod(allDomains[i], 1, VIR_DOMAIN_AFFECT_LIVE);
		sleep(1); // sleep to let stats period take effect maybe
		printMemoryStats();
		balanceMemory();
		freeAllDomainPointers();
		virConnectClose(hypervisor);
		sleep(sleepInterval);
	}
}


int populateAllDomainsArray() {
	numOfDomains = virConnectNumOfDomains(hypervisor);
	allDomains = malloc(numOfDomains * sizeof(virDomainPtr));
	int res = virConnectListAllDomains(hypervisor, &allDomains, VIR_DOMAIN_MEMORY_STAT_NR);
	return res;
}

int freeAllDomainPointers() {
	for (int i = 0; i < numOfDomains; i++) virDomainFree(allDomains[i]);
	free(allDomains);
	return 0;
}

/***
9-20-2020:

Status of test cases ( with an interval of 1 second ):

Test 1: 

The VM they run the test on slowly ramps up memory usage, and program gives the vm
memory as needed. When it frees the memory, the VM gives it up to host. The other VMs
don't give up memory though, because they don't have much allocated to them.

Test 2:
Test 2 seems to run as expected.

Test 3:
Test 3 seems to run as expected.

***/