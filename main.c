#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libvirt/libvirt.h>

#define BIT_SET(a,b) ((a) |= (1ULL<<(b)))

int ONE_MILLION = 1000000;

virDomainPtr* allDomains;

/***
Index is pCPU number. Element at each index is an array of virVCpuInfo structs.
Each element of the struct array is info about the vCPUs assigned to a pCPU.
**/
virVcpuInfoPtr** virtualToPhysicalMap; // danny syas don't do this hhahhaahah

int main() {
   
   virConnectPtr hypervisor = virConnectOpen(NULL);
   balanceCPU(hypervisor);
   int isAlive = virConnectIsAlive(hypervisor); 
   return 0;
}

virVcpuInfoPtr determineVCpuToMove(virVcpuInfoPtr allVCpuInfos[], int vCpuMappings[], int numOfHostCpus, int numOfDomains, virDomainPtr domainToMove) {
  int candidatepCpu;
  /** Can further optimize to choose the best pCPU to make room on. **/
  for(int i = 0; i < numOfHostCpus; i++ ) if (vCpuMappings[i] > 1) { candidatepCpu = i; break; }
  virVcpuInfoPtr smallestvCpu;
  unsigned int smallestvCpuTime = INT_MAX;
  for(int i = 0; i < numOfDomains; i++) {
    int time = allVCpuInfos[i]->cpuTime/ONE_MILLION;
      if ( time < smallestvCpuTime) {
        smallestvCpuTime = time;
        smallestvCpu = allVCpuInfos[i];
        domainToMove = allDomains[i];
      }
  }
  printf("\nFound a vCPU to move. vCpuInfo:\n");
  printf("\nvCPU Number: %i, vCpu State: %i, vCpu CPU Time (ms): %llu, pCpu Number: %i\n", smallestvCpu->number, smallestvCpu->state, smallestvCpu->cpuTime / ONE_MILLION, smallestvCpu->cpu);
  printf("\nvCpu to move is on domain: %s\n", virDomainGetName(domainToMove));
  return smallestvCpu;
}


int balanceCPU(virConnectPtr hypervisor) {
   int numOfDomains = virConnectNumOfDomains(hypervisor);
   printf("\nNumber of domains: %i", numOfDomains);
   printf("\nGetting pointers to all domains...");
   allDomains = malloc(numOfDomains * sizeof(virDomainPtr));
   int res = virConnectListAllDomains(hypervisor, &allDomains, VIR_CONNECT_LIST_DOMAINS_ACTIVE);
   

   balanceCpuIfNeeded(hypervisor, allDomains, numOfDomains);
   


	return 0;
}

void balance(int vCpuMappings[],
             int numOfHostCpus,
             virVcpuInfoPtr allVCpuInfos[],
             double timeMappings[],
             int numOfDomains,
             virDomainPtr* allDomains) {
   /** This actually needs vcpu cpumaps to not only know what pCPU
   is available for pinning, but exactly which vCPU (and its domain) will
   be pinned to what pCPU.
   
   We should already know via timemappings and vCpuMappings arrays which
   pCPU is available for scheduling. We need to now choose which vCPU to
   schedule.
   ***/
	/* approaches to knowing which vCPU is where on whic pCPU:
	find the pCPU with > 1 vCPU and highest utilization.
  Iterate over vCpuInfo array, find all vCPUs on that pCPU, choose the one with the smallest execution time.
  **/
   printf("\nbalance() function.\n");
   int pCpuToPin = -1;
   printf("First attempting to find an empty pCPU to pin to...\n");
   for(int i = 0; i < numOfHostCpus; i++) {
   	if (vCpuMappings[i] == 0) {
   		pCpuToPin = i;
   		break;
   	}
   }
   if (pCpuToPin == -1) {
    printf("No empty pCPUs were available. Finding the least utilized one...\n");
   	double min = DBL_MAX;
   	int leastUtilizedpCpu = -1;
   	for(int i = 0; i < numOfHostCpus; i++) {
   		if (timeMappings[i] < min) { min = timeMappings[i]; leastUtilizedpCpu = i; }
   	}
    printf("Least utilized pCPU: %i\n", leastUtilizedpCpu);
    pCpuToPin = leastUtilizedpCpu;
   }
   printf("\npCPU to pin a vCPU to: %i", pCpuToPin);
   printf("Finding which vCPU to pin to pCPU %i...\n", pCpuToPin);
  
   virDomainPtr domainToMove; // OUT ARG
   virVcpuInfoPtr vCpuToMove = determineVCpuToMove(allVCpuInfos, vCpuMappings, numOfHostCpus, numOfDomains, domainToMove);
   pinvCpuTopCPU(vCpuToMove, pCpuToPin, domainToMove);
}

void pinvCpuTopCPU(virVcpuInfoPtr vCpu, int pCpu, virDomainPtr domain) {
     unsigned char cpuMap[1] = { 0 };
     int n = 8;
     BIT_SET(cpuMap[0], 7 - pCpu);
     int res = virDomainPinVcpu(domain, vCpu->number, cpuMap, 1);
     printf("\nReturn code from virDomainPinVcpu: %i \n", res);
}


int balanceCpuIfNeeded(virConnectPtr hypervisor, virDomainPtr* allDomains, int numOfDomains) {
   virVcpuInfoPtr vCpuInfo = malloc(sizeof(virVcpuInfo));
   virVcpuInfoPtr allVCpuInfos[numOfDomains]; // assume 1 vCPU per domain.
   int numOfHostCpus = virNodeGetCPUMap(hypervisor, NULL, NULL, 0);
   printf("\nFound %i real CPUs on host.\n", numOfHostCpus);


   

   /** Variable timeMappings is a map of total vCPU CPU time for
       each pCPU. The index of this array is the number assigned
       to the pCPU. The contents of the array at each index are
       the cumulative CPU time across all domains for that pCPU.
       **/
   double timeMappings[numOfHostCpus]; // in ms
   memset(timeMappings, 0, numOfHostCpus*sizeof(double));

   /** Variable vCpuMappings is a map of total number of vCPUs
       currently pinned to each pCPU. The index of this array is 
       the number assigned to the pCPU. The contents of the array
       at each index are the number of vCPUs pinned to that pCPU. 
       **/
   int vCpuMappings[numOfHostCpus];
   memset(vCpuMappings, 0, numOfHostCpus*sizeof(int));

   for(int i = 0; i < numOfDomains; i++) {
   	 // 8 VMs, each with 1 vCPU for CPU part when grading.
   	 // they confirmed we assume 1 vCPU per VM.
   	    unsigned char ** unknownCpuMap = malloc(sizeof(char*));
   		int vCpuRes = virDomainGetVcpus(allDomains[i], vCpuInfo, 3, *unknownCpuMap, sizeof(*unknownCpuMap)); // what does cpumap do here?
   		printf("\nvCPU Number: %i, vCpu State: %i, vCpu CPU Time (ms): %llu, pCpu Number: %i\n", vCpuInfo->number, vCpuInfo->state, vCpuInfo->cpuTime / ONE_MILLION, vCpuInfo->cpu);
	    timeMappings[vCpuInfo->cpu] = timeMappings[vCpuInfo->cpu] + (vCpuInfo->cpuTime/ONE_MILLION);
	    vCpuMappings[vCpuInfo->cpu] = vCpuMappings[vCpuInfo->cpu] + 1;
      allVCpuInfos[i] = vCpuInfo;    
	}


     //If ANY pCPU has more than one vCPU AND there are empty pCPUs, unbalanced. 
  // If there are no empty CPUs, and a pCPU has > 1 vCPU, check time usage. 
    int emptyCpus = 0;
    int balanced = 1;
    int pCpuWithMoreThanOnevCpu = 0;

    for(int i = 0; i < numOfHostCpus; i++) if (vCpuMappings[i] == 0) emptyCpus = 1;
    for(int i = 0; i < numOfHostCpus; i++) {
      if (vCpuMappings[i] > 1) pCpuWithMoreThanOnevCpu = 1;
    	if (vCpuMappings[i] > 1 && emptyCpus == 1) balanced = 0;
	}
  if (balanced && emptyCpus == 0 && pCpuWithMoreThanOnevCpu == 1) {
    printf("\nThere are no empty pCPUs, and some pCPUs have more than one vCPU.\n");
    printf("\nFinding balance based on time usage...\n");
    unsigned int averageTime;
    for(int i = 0; i < numOfHostCpus; i++) averageTime = averageTime + timeMappings[i];
    averageTime = averageTime / numOfHostCpus;
    printf("\nAverage time: %i", averageTime);
    double unbalancedPercent = 0.1;
    double threshold = averageTime + (averageTime*unbalancedPercent);
    for(int i = 0; i < numOfHostCpus; i++) if (timeMappings[i] > threshold) balanced = 0;
    printf("\nThreshold time value over which system is unbalanced: %f\n", threshold);
}
    printf("\nisBalanced: %i\n", balanced);
	if (!balanced) balance(vCpuMappings, numOfHostCpus, allVCpuInfos, timeMappings, numOfDomains, allDomains);
	return 0;
}

/****

I think virNodeGetCPUMap gives us a map of all CPUs on the host that are online. It's a bitmap,
so...

 CPU0-7, 8-15... In each byte, lowest CPU number is least significant bit.

 On my physical desktop I have 4 CPU cores, so I think this will look like

 00001111
 0xF

 Which is what I'm getting.

 The above has capacity for 8 CPUs though. Need to generalize. 




 Two strategies I see for balancing workload across pCPUs.
 The instructor said we can assume no other large workloads
 will be happening on host, so we can assume that virtual 
 CPU time reported by domains is roughly the total load
 on physical CPUs.


 Approach 1:

 Try to balance the number of vCPUs pinned to each pCPU. 
 The vCPU<->pCPU mapping is found using virDomainGetVcpus().

 Approach 2:

 Try to balance the vCPU time usage across pCPUs. This will 
require me to assign a weight to each vCPU. 

virVcpuInfo struct gives vCPU's pCPU number and pCPU time used in nanoseconds. 
If I iterate over all vCPUs and create a map of pCPUs and the time used on them,
I can determine which pCPUs are being underutilized by the guests.
I can then choose a pCPU to give more work to. 



 "where every pCpu handles similar amount of workload."
 "Once the VMs enter into the stable/balanced state, your program shouldn’t do additional jobs."
 So if the CPUs are balanced, don't re-balance them. Define what "balanced" looks like. 
Take average of all pCPUs, then iterate again and find the one most out of bounds.
If they're all within 10% of avg or something they're "balanced".
We don't have unique IDs for vCPUs, but we have a key such as
domainname+vCPUNumber. string array?

Balancing:
If average pCPU usage is lower than all vCPU CPU time stats, then we're balanced.

If any vCPU CPU time stat is higher than (10%?) the average, unbalanced. 


What if multiple vCPUs get pinned to a pCPU, but other pCPUs are open?

10 5 0 0

avg: 15/4 = 3.75

10 > (3.75 + (3.75*20)), unbalanced.

What if # of pCPUs >> vCPUs?

5 10 0 0 0 0 0 0 0 0 0 0 0 0 0 0
 avg: 15/16 = .937
 5 > (.937 + (.937 * .20))...unbalanced. but it should be balanced. 

 If ANY pCPU has more than one vCPU AND there are empty pCPUs, unbalanced. 
 That handles it. I've seen this in the wild where both vCPUs are pinned
 to pCPU 0. Is that a special case? Is the balancing algorithm:

 1. Distribute vCPUs such that every vCPU has its own dedicated pCPU.
 2. When pCPUs are full, then distribute based on execution time.
    For each pCPU that has > 1 vCPU:
     a. Find a new place for the vCPU (the pCPU with the smallest CPU time usage)
     b. Pin the vCPU to that new pCPU. 

***/


int balanceMemory(virConnectPtr hypervisor) {
    /***
        Create map of all guests. What their memory allocation is, 
        what % of it is being used in them. Be able to compare them
        all and choose how to balance physical memory allocation.
    ***/
/*
   int numOfDomains = virConnectNumOfDomains(hypervisor);
   printf("\nNumber of domains: %i", numOfDomains);
   printf("\nGetting pointers to all domains...");
   virDomainPtr* allDomains = malloc(numOfDomains * sizeof(virDomainPtr));

    unsigned long vMemRes = virDomainGetMaxMemory(allDomains[i]);

   		virDomainMemoryStatStruct stats[15];
   		int memStats = virDomainMemoryStats(allDomains[i], stats, 15, 0);
		printf("\nNumber of stats returned: %i", memStats);
        for(int i = 0; i < memStats; i++) {
        	printf("\nTag: %x\nVal: %lli", stats[i].tag, stats[i].val);
        }

*/
	return 0;
}
