### check_ent_pools is a combined monitor for entitlement and pool monitoring
### check_entitlement monitors just entitlement usage
### check_pools monitors just pool usage

See INSTALL on how to compile and install the set of monitors!
See command line option --help for details about all options!

## LPAR prerequisites

The monitor runs on Power5/6/7/8/9 hardware with shared processor LPARs or
dedicated donating LPARs.
```
$ lparstat -i|grep -E "Type|Mode"
```
will show values like Shared-SMT, Shared, Shared-SMT-4 or Donating, Donating-SMT...
When running a donating LPAR, "Mode" will show "donating"

To be able to monitor pool data, the option "Enable performance collection" in
the LPAR profile must be set!

It is always useful to check if nmon (option p) shows sane entitlement and pool data!
There were bugs in certain AIX levels resulting in wrong or even no performance data at all.


## Monitoring LPAR entitlement and vCPU usage

These monitors are avaliable in check_ent_pools and check_entitlement and work on shared and
dedicated donating LPARs.

* -ew and -ec monitor the consumed entitlement over the check interval.
Valid values are absolute values or percentage values, you can specify even both at the same time:
e.g. -ec 3.5 -ec 200% will set thresholds to 3.5 CPUs _and_ whatever 200% of the LPAR entitlement is.
Percentage values apply to the configured entitlement value of the LPAR.
Percentage values range from 1% to 2000% representing the minimum entitlment of 0.05
for a LPAR with 1 vCPU.
Absolute values are positive floating point numbers with 1 decimal place.

The monitor does not enforce values to match the possible maximum! That means the threshold can be
set to 6 even though the LPAR has only 3 vCPUs, or to 2000% on a 1.0 entitlement LPAR with 2 vCPUs.
I don't consider this a bug :-) Convince me when you think it is one!

Warning (-ew) and critical (-ec) options can be placed independently, e.g. it's possible to create only
critical events but no warnings.

* -vbw and -vbc monitor the number of virtual CPUs busy and take only percentage values (1..100%).
-vbc 95% will generate critical events, when the entitlement usage of the LPAR is higher than 95%
of the configured number of vCPUs.


## Monitoring shared cpu pools

These monitors are avaliable in check_ent_pools and check_cpu_pools and work on shared LPARs only.
"Enable performance collection" needs to be enabled on the HMC.

The monitors will measure usage of the shared CPU pool the LPARs is a member of.
Pool usage of different CPU pools can not be monitored on one LPAR!
```
$ lparstat -i|grep "Shared Pool ID"
```
will show the monitored CPU pool.

* -pw and -pc monitor the entitlement consumption of the current CPU pool the monitor runs on.
Thresholds can be absolute values representing entitlement consumption or percentage values
representing relative consumption applied to the pool size.
Absolute values and percentage values can be used at the same time: e.g. -pw 10 -pw 90%

**Attention**: The size of pool 0 is always equal to the number of available CPUs for all available
shared CPU pools, but the utilization data includes only pool 0 LPARs! Be careful to monitor
pool 0 LPARs, especially when there are other CPU pools!
To monitor the managed system utilization, DO NOT monitor pool 0, use the system pool monitor!
The size of pool 0 is "variable" when dedicated donating LPARs are used.

* -pfw and -pfc are used to monitor for free capacity. -pfc 2 will generate critical events when the
CPU pool has less than 2 CPUs free. Same applies to percentage values, you can have both at the
same time.

Maximum hardware limits are not enforced for thresholds.


## Monitoring the global or system pool

These monitors are avaliable in check_ent_pools and check_cpu_pools and work on shared LPARs only.
"Enable performance collection" needs to be enabled on the HMC.

The monitors will measure the utilization of the whole managed system (global or system pool),
including all the various CPU pools and the hypervisor.
```
$ lparstat -i|grep "Shared Physical CPUs in system"
```
will show the number of CPUs in the system pool.

* -sw and -sc monitor the entitlement consumption of the system pool.
Thresholds are absolute values representing the entitlement consumption of the managed system or
percentage values representing relative consumption of all available CPUs.
Both absolute and percentage values can be used at the same time.

* -sfw and -sfc monitor the free capacity in the system pool.
Use absolute and/or percentage values to check the amount of free entitlement in the managed system.

Maximum hardware limits are not enforced for thresholds.

From experience, the hypervisor uses up to 1.0 entitlement on a p770, maybe more on larger machines,
maybe less on smaller machines.
Take this in mind when setting thresholds to close to the maximum hardware limit! Alarming may be
to late...

The consumption of dedicated LPARs is invisible to this monitor. Dedicated LPARs simply reduce the
amount of available pool CPUs.

**Important**: Dedicated donating LPARs dynamically reduce the size of the system pool. This might lead
to confusion, espescially when relative percentages are used for monitoring.


## Check interval

Performace values are calculated as average over a certain period of time.
Default interval is 1 second, maximum is 30 seconds.
Be careful with high values, you may need to adjust the nagios plugin timeout!


## Strict checking

Sometimes IBM manages to screw things like firmware, kernel or performance library.
Check e.g. IV33883 for details.
When this happens, most of the thresholds are never reached. You'll never notice such situation
because you never get any warning or critical events.
If you're nevertheless interested in getting a notification, use strict checking
(--strict or -x) to receive a critical event.

The current checked values are:
* entitlement usage = 0
* LPAR entitlement = 0
* Size of current CPU pool = 0
* Busy time of current CPU pool = 0
* Number of CPUs in managed system = 0
* Usage of CPUs in managed system = 0
* Number of current pool CPUs > number of CPUs in managed system

More checks will be implemented as the need arises.


## Monitor Output

Because of the high number of thresholds, the output is quite large.
Matching thresholds are printed behind the metric in parentheses. Possible values are OK, WARNING,
CRITICAL. Unused or unmonitored thresholds always evaluate to "OK".
Additional data is included to show the complete picture of the machine state.
Performance data is also printed with all the additional values.

Example for check_ent_pools:
```
ENT_POOLS OK ent_used=0.43(OK) ent=0.50 ent_max=2 vcpu_busy=21.45%(OK) pool_id=11 pool_size=9 pool_used=1.28(OK) pool_free=7.71(OK) syspool_size=16 syspool_used=3.47(OK) syspool_free=12.53(OK) |ent_used=0.43 ent=0.50 ent_max=2 vcpu_busy=21.45 pool_id=11 pool_size=9 pool_used=1.28 pool_free=7.71 syspool_size=16 syspool_used=3.47 syspool_free=12.53
```
* ent_used     : used entitlement of the LPAR
* ent          : Entitled capacity of LPAR (lparstat -i|grep "Entitled Capacity" )
* ent_max      : maximum usable entitlement, same as numer of vCPUs
* vcpu_busy    : percentage of all consumend vCPU (ent/max_ent\*100)
* pool_id      : shared cpu pool id of this LPAR (lparstat -i|grep "Shared Pool ID")
* pool_size    : size of the shared cpu pool "pool_id" (lparstat -i|grep "Active CPUs in Pool")
* pool_used    : used entitlement of the pool "pool_id"
* pool_free    : free entitlement in the pool "pool_id"
* syspool_size : size of the system cpu pool (lparstat -i|grep "Shared Physical CPUs in system")
* syspool_used : used entitlement of the system shared cpu pool
* syspool_free : free entitlement in the system shared cpu pool


## Thanks

Thanks go to Michael Perzl for supplying me with a working getopt_long for AIX and all the people from
the AIX Developer Works forums for answering my stupid questions.


## Bugs

None known at the moment.

