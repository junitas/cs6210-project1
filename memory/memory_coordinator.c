#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libvirt/libvirt.h>

#define BIT_SET(a,b) ((a) |= (1ULL<<(b)))

#define BYTES_IN_KIBIBYTE 1024

#define DEFAULT_DEBUG_VALUE 69

double MEMORY_USAGE_RED_ALERT = 75;

int numOfDomains;
virDomainPtr* allDomains;
virConnectPtr hypervisor;

typedef struct domainMemoryNeeds {
  int domainNumber;
  double domainUsageRatio;
  unsigned long long memoryNeededToGetToThreshold; //total kb needed to get domainUsageRatio down to 0.75.
} domainMemoryNeeds;



/***

It's okay to just give it to a domain from the host, I think. 
If all of them are at 0.95 usage, we can't take from any.
During downtime we can take from ones that aren't using their memory
and give it to host.


***/
int domainToTakeFrom() {
	return 0;
}


int giveAdditionalMemory(domainMemoryNeeds domainToGiveMemory) {
	int res = virDomainSetMemory(allDomains[domainToGiveMemory.domainNumber],
		  domainToGiveMemory.memoryNeededToGetToThreshold);
	return res;
}

domainMemoryNeeds domainNeedsMemory() {
	domainMemoryNeeds domainToGiveMemory = {-1,0.0,0};
	double highestMemoryUsageRatio = 0.0;
	for(int i = 0; i < numOfDomains; i++) {
		virDomainMemoryStatPtr stats = calloc(VIR_DOMAIN_MEMORY_STAT_NR, sizeof(virDomainMemoryStatStruct));
		int maxMem = virDomainGetMaxMemory(allDomains[i]);
		int numRet = virDomainMemoryStats(allDomains[i], stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);
		unsigned long long unused = DEFAULT_DEBUG_VALUE;
		unsigned long long available = DEFAULT_DEBUG_VALUE;
		unsigned long long balloonSize = DEFAULT_DEBUG_VALUE;
		unsigned long long usable = DEFAULT_DEBUG_VALUE;
       
		for(int j = 0; j < numRet; j++) {
			virDomainMemoryStatStruct s = stats[j];
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_UNUSED) unused = s.val;
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE) available = s.val;
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) balloonSize = s.val;
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_USABLE) usable = s.val;
		}

		// If the ratio is above 0.75, and this VM doesn't
		// already have too much memory allocated, and it 
		// has the worst memory ratio
        double r = (100.0) - ((float)unused/(float)(available))*(100);
		if (r >= MEMORY_USAGE_RED_ALERT && r >= highestMemoryUsageRatio) {
             highestMemoryUsageRatio = r;
             domainToGiveMemory.domainNumber = i;
             domainToGiveMemory.domainUsageRatio = r;
             domainToGiveMemory.memoryNeededToGetToThreshold = available + (-1)*((available*(MEMORY_USAGE_RED_ALERT-100))/100);
		}

	}
	return domainToGiveMemory;
}


int balanceMemory() {
	domainMemoryNeeds domainToGiveMemory = domainNeedsMemory();
	
		// What if a VM is using less than 0.75 of its memory?
		// I guess leave it alone.
	if (domainToGiveMemory.domainNumber == -1) {
		printf("No domain has above 0.75 memory usage.\n");
		return 0;
	}

    //int domainToTakeMemoryFrom = domainToTakeFrom(); // only to give to host

	printf("\nDomain %s to be given more memory.\n\n", virDomainGetName(allDomains[domainToGiveMemory.domainNumber]));
	printf("domainToGiveMemory.memoryNeededToGetToThreshold: %llu\n", domainToGiveMemory.memoryNeededToGetToThreshold);
	    
    int res = giveAdditionalMemory(domainToGiveMemory);
    if (res == -1) printf("Failed to give the domain memory.\n");
    else printf("Successfully gave the domain memory.\n");
	return 0;


}

int printMemoryStats() {
	for(int i = 0; i < numOfDomains; i++) {
		virDomainMemoryStatPtr stats = calloc(VIR_DOMAIN_MEMORY_STAT_NR, sizeof(virDomainMemoryStatStruct));
		printf("\nDomain: %s\n", virDomainGetName(allDomains[i]));
		int maxMem = virDomainGetMaxMemory(allDomains[i]);
		printf("Max physical memory allocated to domain: %uKiB = %uKB\n", maxMem, maxMem);
		int numRet = virDomainMemoryStats(allDomains[i], stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);
		printf("Got %i stats about domain.\n", numRet);
		unsigned long long unused = DEFAULT_DEBUG_VALUE;
		unsigned long long available = DEFAULT_DEBUG_VALUE;
		unsigned long long balloonSize = DEFAULT_DEBUG_VALUE;
		unsigned long long usable = DEFAULT_DEBUG_VALUE;
       
		for(int j = 0; j < numRet; j++) {
			virDomainMemoryStatStruct s = stats[j];
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_UNUSED) unused = s.val;
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE) available = s.val;
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) balloonSize = s.val;
			if (s.tag == VIR_DOMAIN_MEMORY_STAT_USABLE) usable = s.val;
		}
		printf("Memory left unused by domain: %llukB\n", unused);
		printf("Memory usable by domain:      %llukB\n", available);
		printf("Current balloon size:         %llukB\n", balloonSize);
		printf("How much the balloon can be inflated without pushing the guest system to swap: %llukB\n", usable);
		printf("Memory usage: %f\n", (100.0) - ((float)unused/(float)(available))*(100));
	}
}


int main(int argc, char * argv) {
	  while(1) {
      hypervisor = virConnectOpen(NULL);

      populateAllDomainsArray();
      for(int i = 0; i < numOfDomains; i++) virDomainSetMemoryStatsPeriod(allDomains[i], 1, 0);
      sleep(2);
      printMemoryStats();
      balanceMemory();
      freeAllDomainPointers();
      virConnectClose(hypervisor);
      sleep(5);	
  }
}


int populateAllDomainsArray() {
   numOfDomains = virConnectNumOfDomains(hypervisor);
   printf("\nNumber of domains: %i", numOfDomains);
   printf("\nGetting pointers to all domains...");
   allDomains = malloc(numOfDomains * sizeof(virDomainPtr));
   int res = virConnectListAllDomains(hypervisor, &allDomains, VIR_DOMAIN_MEMORY_STAT_NR);
   return res;
}

int freeAllDomainPointers() {
	printf("\nFreeing all domain pointers...\n");
    for(int i = 0; i < numOfDomains; i++) virDomainFree(allDomains[i]);
    return 0;
}


/***

The goal should be to maximize total memory
UTILIZATION on the host machine while still
giving all guests their fair share of memory.


VMWare uses the idle tax ballooning approach, which
I will use as well. or will i? do i have that info?


Mem.IdleTax – default: 75, range: 0 to 99, specifies the percent of idle memory
that may be reclaimed by the tax


TODO:

Learn libvirt API for getting memory usage stats per guest,
and information about amount of used and amount of idle.

Figure out how the policy works. When do we take memory from a guest?
How do we take memory from a guest? How do we give it to another guest?
This coordinator, per project requirements, should react properly when
memory resources in a guest are insufficient.
***/


/***


Possibly important functions for this:

virDomainSetMemoryStatsPeriod(...) // called out by profs
virDomainGetMaxMemory(...)
virDomainMemoryStats(...) -> virDomainMemoryStatTags
virDomainSetMaxMemory(....)			
int	virDomainSetMemory(...)
int	virDomainSetMemoryFlags(...)
int	virDomainSetMemoryParameters(...)
**/


/***

virDomainInfo ¶
struct virDomainInfo {
unsigned char	state	
the running state, one of virDomainState
unsigned long	maxMem	
the maximum memory in KBytes allowed
unsigned long	memory	
the memory in KBytes used by the domain
unsigned short	nrVirtCpu	
the number of virtual CPUs for the domain
unsigned long long	cpuTime	
the CPU time used in nanoseconds
} 



**/