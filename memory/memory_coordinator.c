#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libvirt/libvirt.h>

#define BIT_SET(a,b) ((a) |= (1ULL<<(b)))

#define BYTES_IN_KIBIBYTE 1024

#define DEFAULT_DEBUG_VALUE 69

double MEMORY_USAGE_RED_ALERT = 80; // Median of a range.
int PAGE_SIZE_IN_KB = 4;
int PAGES_TO_ALLOCATE_AT_ONCE = 4500;
const double MIN_MEM_AVAILABLE_ON_HOST = 0.15;

int numOfDomains;
virDomainPtr* allDomains;
virConnectPtr hypervisor;

int printMemoryStats() {
	printf("\nMemory Statistics:\n");
	printf("---------------------");
	for (int i = 0; i < numOfDomains; i++) {
		virDomainMemoryStatPtr stats = calloc(VIR_DOMAIN_MEMORY_STAT_NR, sizeof(virDomainMemoryStatStruct));
		printf("\nDomain: %s\n", virDomainGetName(allDomains[i]));
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
		printf("Memory usable by domain:      %llukB\n", available);
		printf("Current balloon size:         %llukB\n", balloonSize);
		//printf("How much the balloon can be inflated without pushing the guest system to swap: %llukB\n", usable);
		printf("Memory usage: %f percent\n", (100.0) - ((float)unused / (float)(available)) * (100));
	}
	printf("---------------------\n");
}

void giveMemory(virDomainPtr domain, unsigned long long balloonSize) {

	/** Before giveMemory is called, takeMemoryAway should have been called already.
	  * This means any VM that could give up memory already has, and the host has
	  * all of the memory it could get. **/
	virNodeInfoPtr nodeInfo = calloc(1, sizeof(virNodeInfo));
	virNodeGetInfo(hypervisor, nodeInfo);

	double freeMem = virNodeGetFreeMemory(hypervisor) / (1024.0); // kB
	double totalMem = nodeInfo->memory; //kB
	free(nodeInfo);
	if (freeMem / totalMem < MIN_MEM_AVAILABLE_ON_HOST) {
		printf("Host free memory: %f percent. Less than %f memory available to host OS. Cannot give a VM more memory.\n", freeMem / totalMem, MIN_MEM_AVAILABLE_ON_HOST);
		
		return;
	}
	const char* domainName = virDomainGetName(domain);

	if (virDomainGetMaxMemory(domain) >= (balloonSize + (PAGE_SIZE_IN_KB * PAGES_TO_ALLOCATE_AT_ONCE))) {
		printf("Domain %s cannot be given any more memory or it will exceed its maximum allowed.\n", domainName);
		return;
	}

	// basing it off of balloon size works which makes no sense. whatever.
	printf("Giving domain %s %i kB\n", domainName, PAGE_SIZE_IN_KB * PAGES_TO_ALLOCATE_AT_ONCE);
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

		// this ratio might not be right. when the test starts using less
		//memory, this ratio doesn't go down.
		/** 9-19-2020. r is a function of unused and available. But as r changes, we also
		change "available", so this isn't necessarily correct. unused/vs max? idk**/
/*
Memory (VM: aos_vm3)  Actual [139.48046875], Unused: [53.38671875]
Memory left unused by domain: 54668kB
Current balloon size:         142828kB*/
 /** Here we see that in the output of "monitor", "actual" seems to be closer to what
 we call "current balloon size"

		double r = (100.0) - ((float)unused / (float)(available)) * (100);
		if ( r <  (MEMORY_USAGE_RED_ALERT - .10)) takeMemoryAway(allDomains[i], balloonSize);
		if ( r > (MEMORY_USAGE_RED_ALERT + .10)) giveMemory(allDomains[i], balloonSize);
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
		sleep(1); // let stats period be set
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
9-15-2020:

Max physical memory allocated to domain: 262144KiB = 262144KB
Value returned from virDomainGetMaxMemory: 262144
Memory left unused by domain: 49508kB
Memory usable by domain:      241644kB

So when 4 VMs are running, they're up in the 85% range
of memory "used". When one's running, like 40%. I'm wondering if
that percentage is unused/available and has nothing to do with
"max". As in if the percentage gets hgih, we just start giving
the VM a bit more memory until it gets good.

hmmm..
maybe loop through them all.
if r < 75%, take memory away.
if r == 75% do nothin (or between 70 and 80)
if r > 75%, give memory.

***/