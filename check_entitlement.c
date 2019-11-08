/*
 * monitors entitlement usage on shared processor LPARs
 * monitors entitlement usage on dedicated donating LPARs
 * not usable on dedicated LPARs
 * compiles with IBM xlC and gcc on AIX 5.3(>TL6), 6.1 and 7.1
 * 
 * Compile with: cc -o check_entitlement -lperfstat check_entitlement.c
 *
 * This is stripped down from check_ent_pools.c, only pool parts removed
 *
 * This nagios plugin comes with ABSOLUTELY NO WARRANTY. You may redistribute
 * copies of the plugin under the terms of the GNU General Public License.
 * For more information about these matters, see the file named COPYING.
*/
const char *progname = "check_entitlement";
const char *program_name = "check_entitlement";
const char *copyright = "2014,2019";
const char *email = "megabreit@googlemail.com";
const char *name = "Armin Kunaschik";
const char *version = "1.4";

#include <macros.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <libperfstat.h>

#ifndef XINTFRAC		/* for timebase calculations... */
#include <sys/systemcfg.h>	/* only necessary in AIX 5.3, AIX >=6.1 defines this in libperfstat.h */
#define XINTFRAC    ((double)(_system_configuration.Xint)/(double)(_system_configuration.Xfrac))
#endif

/* include GNU getopt_long, since AIX does not provide it */
#include "getopt_long.h"
#include "getopt_long.c"

/* define Nagios return codes */
enum {
        STATE_OK,
        STATE_WARNING,
        STATE_CRITICAL,
        STATE_UNKNOWN,
        STATE_DEPENDENT
};

/* string representations of the return codes */
char *states[5]={"OK","WARNING","CRITICAL","UNKNOWN","DEPENDENT"};

/* initial state values */
int ent_pool_state=STATE_OK, temp_state=STATE_OK;
int vcpu_busy_state=STATE_OK;
int ent_state=STATE_OK;

int dedicated_donating=0;	/* marker for dedicated donating mode */
int interval=1;			/* default interval in seconds between the 2 perflib calls = monitoring period */
int verbose=FALSE;		/* only 1 verbose level... violating the plugin recommendations here */
int strict=FALSE;		/* additional sanity checking of various system values */

/* monitoring variables */
double entitlement_critical_pct=0, entitlement_warning_pct=0;
double entitlement_critical=0, entitlement_warning=0;
double vcpu_busy_critical_pct=0, vcpu_busy_warning_pct=0;


/* helper functions "stolen" from util.c and utilbase.c */
int is_numeric (char *number)
{
	char tmp[1];
	float x;

	if (!number)
		return FALSE;
	else if (sscanf (number, "%f%c", &x, tmp) == 1)
		return TRUE;
	else
		return FALSE;
}

int is_positive (char *number)
{
	if (is_numeric (number) && atof (number) > 0.0)
		return TRUE;
	else
		return FALSE;
}


int is_percentage (char *number)
{
        int x;
	if (is_numeric (number) && (x = atof (number)) >= 0 && x <= 100)
		return TRUE;
	else
		return FALSE;
}

int is_integer (char *number)
{
	long int n;

	if (!number || (strspn (number, "-0123456789 ") != strlen (number)))
		return FALSE;

		n = strtol (number, NULL, 10);

		if (errno != ERANGE && n >= INT_MIN && n <= INT_MAX)
			return TRUE;
		else
			return FALSE;
}

int is_intpos (char *number)
{
	if (is_integer (number) && atoi (number) > 0)
		return TRUE;
	else
		return FALSE;
}

/* modified is_intpercent starting from 1% */
int is_intpercent (char *number)
{
	int i;
	if (is_integer (number) && (i = atoi (number)) >= 1 && i <= 100)
		return TRUE;
	else
		return FALSE;
}

/* entitlement percentage ranges from 1 to 2000%
 * a LPAR with minimum entitlement of 0.05 per 1 virtual processor is able to "consume" 2000% CPU */
int is_intpercent_ent (char *number)
{
	int i;
	if (is_integer (number) && (i = atoi (number)) >= 1 && i <= 2000)
		return TRUE;
	else
		return FALSE;
}

int max_state (int a, int b)
{
        if (a == STATE_CRITICAL || b == STATE_CRITICAL)
                return STATE_CRITICAL;
        else if (a == STATE_WARNING || b == STATE_WARNING)
                return STATE_WARNING;
        else if (a == STATE_OK || b == STATE_OK)
                return STATE_OK;
        else if (a == STATE_UNKNOWN || b == STATE_UNKNOWN)
                return STATE_UNKNOWN;
        else if (a == STATE_DEPENDENT || b == STATE_DEPENDENT)
                return STATE_DEPENDENT;
        else
                return max (a, b);
}

/* get monitor status when values are higher than thresholds
 * if warn or crit is 0 we assume, the value is not monitored and return STATE_OK */
int get_status(double value, double warn, double crit)
{
	if (crit > 0 && value > crit) {
		return STATE_CRITICAL;
	}
	if (warn > 0 && value > warn) {
		return STATE_WARNING;
	}
	return STATE_OK;
}

/* get monitor status when values are higher than thresholds (verbose message included)
 * if warn or crit is 0 we assume, the value is not monitored and return STATE_OK */
int get_new_status(char *verbose_message, double value, double warn, double crit)
{
	int state = STATE_OK;

	if (warn > 0 && value > warn) {
		state = STATE_WARNING;
	}
	if (crit > 0 && value > crit) {
		state = STATE_CRITICAL;
	}

	if (verbose) {
		printf("%s state -> %s (val=%.2f warn>%.2f crit>%.2f)\n",
			verbose_message,
			states[state],
			value,
			(warn==0?NAN:warn), /* display NAN means, values was not used for comparison */
			(crit==0?NAN:crit)
			);
	}
	return state;
}

/* get monitor status when values are lower than thresholds
 * if warn or crit is 0 we assume, the value is not monitored and return STATE_OK */
int get_lower_status(double value, double warn, double crit)
{
	if (crit > 0 && value < crit) {
		return STATE_CRITICAL;
	}
	if (warn > 0 && value < warn) {
		return STATE_WARNING;
	}
	return STATE_OK;
}

/* get monitor status when values are lower than thresholds (verbose message included)
 * if warn or crit is 0 we assume, the value is not monitored and return STATE_OK */
int get_new_lower_status(char *verbose_message, double value, double warn, double crit)
{
	int state = STATE_OK;
	if (warn > 0 && value < warn) {
		state = STATE_WARNING;
	}
	if (crit > 0 && value < crit) {
		state = STATE_CRITICAL;
	}
	
	if (verbose) {
		printf("%s state -> %s (val=%.2f warn<%.2f crit<%.2f)\n",
			verbose_message,
			states[state],
			value,
			(warn==0?NAN:warn), /* display NAN means, values was not used for comparison */
			(crit==0?NAN:crit)
			);
	}
	return state;
}

void print_version(const char *progname,const char *version)
{
	printf("%s v%s\n",progname,version);
	exit(0);
}

void print_usage (void)
{
	printf ("%s\n", _("Usage:"));
 	printf (" %s [ -ec=limit ] [ -ew=limit ] [ -vbw=limit ] [ -vbc=limit ] [ -i=interval ] [ -strict ] [ -h ] [ -v ] [ -V ]\n\n", progname);
}

void print_help (void)
{
	printf ("%s %s\n",progname, version);

	printf ("Copyright (c) %s %s <%s>\n",copyright,name,email);

	printf ("%s\n", _("This plugin checks the CPU entitlement utilization on"));
	printf ("%s\n", _("AIX shared processor or dedicated donating LPARs and generates an alert if"));
	printf ("%s\n", _("values are out of the threshold limits"));

	printf ("\n\n");

	print_usage ();


	printf (" %s\n", "Entitlement monitors:");
	printf (" %s\n", "-ew, --entitlement-warning=VALUE");
	printf ("    %s\n", _("Exit with WARNING status if entitlement usage is higher than VALUE"));
	printf (" %s\n", "-ew, --entitlement-warning=PERCENT%");
	printf ("    %s\n", _("Exit with WARNING status if entitlement usage is higher than PERCENT of"));
	printf ("    %s\n", _("defined entitled capacity"));
	printf (" %s\n", "-ec, --entitlement-critical=VALUE");
	printf ("    %s\n", _("Exit with CRITICAL status if entitlement usage is higher than VALUE"));
	printf (" %s\n", "-ec, --entitlement-critical=PERCENT%");
	printf ("    %s\n", _("Exit with CRITICAL status if entitlement usage is higher than PERCENT of"));
	printf ("    %s\n", _("defined entitled capacity"));
	printf ("\n");
	printf (" %s\n", "vCPU monitors:");
	printf (" %s\n", "-vbw, --vpcu-busy-warning=PERCENT%");
	printf ("    %s\n", _("Exit with WARNING status if entitlement usage is higher than PERCENT of"));
	printf ("    %s\n", _("available vCPU capacity"));
	printf (" %s\n", "-vbc, --vpcu-busy-critical=PERCENT%");
	printf ("    %s\n", _("Exit with CRITICAL status if entitlement usage is higher than PERCENT of"));
	printf ("    %s\n", _("available vCPU capacity"));
	printf ("\n");

	printf (" %s\n", "-i, --interval=INTEGER");
	printf ("    %s\n", _("measurement interval in INTEGER seconds (1..30). Default is 1"));
	printf (" %s\n", "-x, -strict, --strict");
	printf ("    %s\n", _("Exit with CRITICAL status if entitlement values are obviously wrong"));
	printf ("    %s\n", _("e.g. entitlement usage values are 0, number of pool cpus is higher than"));
        printf ("    %s\n", _("installed cpus, LPAR entitlement or CPU usage is 0"));
	printf (" %s\n", "-v, --verbose");
	printf ("    %s\n", _("Show details for command-line debugging"));
	printf (" %s\n", "-h, --help");
	printf ("    %s\n", _("Print help"));
	printf (" %s\n", "-V, --version");
	printf ("    %s\n", _("Show plugin version"));


	printf ("\n");
	printf ("%s\n", _("VALUE is a non-negative floating point number with 1 decimal place e.g. 3.1"));
	printf ("%s\n", _("INTEGER is a positive integer number"));
	printf ("%s\n", _("PERCENT is a positive integer number in the range 1..100"));
	printf ("%s\n", _("PERCENT in entitlement arguments are in the range 1..2000"));
	printf ("\n");
	printf ("%s\n", _("Warning and critical checks can be configured independently, critical checks"));
        printf ("%s\n", _("without warnings are possible"));
	printf ("%s\n", _("Exactly one PERCENT and one VALUE for each option is possible, see examples"));
	printf ("\n");
	printf ("%s\n", _("Examples:"));
	printf ("\n");
	printf ("%s\n", _("Checks entitlement usage at 2.0 and 2.5 or 200%:"));
	printf ("%s\n", _("check_entitlement -ew 2.0 -ec 2.5 -ec 200%"));
	printf ("\n");
	printf ("%s\n", _("Checks entitlement usage at 100% and 300%:"));
	printf ("%s\n", _("check_entitlement -ew 100% -ec 300%"));
	printf ("\n");

	printf ("\n");
	printf ("This nagios plugin comes with ABSOLUTELY NO WARRANTY. You may redistribute\n");
	printf ("copies of the plugin under the terms of the GNU General Public License.\n");
	printf ("For more information about these matters, see the file named COPYING.\n");

}

/* main */
int main(int argc, char* argv[])
{
    int c;
    int option_index = 0;

    static struct option long_options[] = {
	{"ec",                   required_argument, 0, 'e'},
	{"entitlement-critical", required_argument, 0, 'e'},
	{"ew",                   required_argument, 0, 'f'},
	{"entitlement-warning",  required_argument, 0, 'f'},
	{"vbw",                  required_argument, 0, 'a'},
	{"virtual-busy-warning", required_argument, 0, 'a'},
	{"vbc",                  required_argument, 0, 'b'},
	{"virtual-busy-critical",required_argument, 0, 'b'},
	{"strict",               no_argument,       0, 'x'},
	{"x",                    no_argument,       0, 'x'},
	{"i",                    required_argument, 0, 'i'},
	{"interval",             required_argument, 0, 'i'},
	{"verbose",              no_argument,       0, 'v'},
	{"version",              no_argument,       0, 'V'},
	{"help",                 no_argument,       0, 'h'},
	{0, 0, 0, 0}
    };

    while (1) {

	c = getopt_long_only (argc, argv, "?Vvh", long_options, &option_index);
    	if (c == -1 || c == EOF)
	    break;

    	switch (c) {
    	case 'V':
	    /* if (verbose) { printf("Option Version selected\n"); } */
	    print_version(progname,version);
	    break;
    	case 'v':
	    verbose=TRUE;
	    /* if (verbose) { printf("Option verbose selected\n"); } */
	    break;
    	case 'e':
	    /* if (verbose) { printf("Option entitlement-critical with %s selected\n",optarg); } */
	    if (strstr(optarg, "%")) {
		    if ( entitlement_critical_pct != 0 ) {
			    printf("ERROR: -ec already set to %.0f%%! Don't specify more than once!\n",entitlement_critical_pct);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is integer 1..2000% */
		    char *tmp=optarg;
		    tmp[strlen(tmp)-1] = 0;	/* remove last char assuming it's the % */
		    entitlement_critical_pct = (double)atoi(optarg);	/* pct ranges from 1 to 2000% for shared LPARs */
	            /* maximum percentage is 2000% only on LPARs with minimum entitlement of 0.05 per virtual processor
	             * effective maximum percentage decreases when entitlement is increased, this is currently not checked for sanity */
		    if ( !(is_intpercent_ent(tmp))) {
			    printf("ERROR: -ec %s%% out of range! Allowed 1%%..2000%%\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold entitlement_critical%%=%.0f%%\n",entitlement_critical_pct); }
	    } else {
		    if ( entitlement_critical != 0 ) {
			    printf("ERROR: -ec already set to %.1f! Don't specify more than once!\n",entitlement_critical);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is floating value */
		    int tmp_int;
		    tmp_int =(int)(atof(optarg)*10); /* we use only 1 digit after the comma, remove all the others */
		    entitlement_critical=(double)tmp_int/10;
		    if ( !(is_positive(optarg))) {
			    printf("ERROR: -ec %s out of range: Entitlement has to be >0 !\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold entitlement_critical=%.1f\n",entitlement_critical); }
	    } 
	    break;
    	case 'f':
	    /* if (verbose) { printf("Option entitlement-warning with %s selected\n",optarg); } */
	    if (strstr(optarg, "%")) {
		    if ( entitlement_warning_pct != 0 ) {
			    printf("ERROR: -ew already set to %.0f%%! Don't specify more than once!\n",entitlement_warning_pct);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is integer 1..2000% */
		    char *tmp=optarg;
		    tmp[strlen(tmp)-1] = 0;	/* remove last char assuming it's the % */
		    entitlement_warning_pct= (double)atoi(optarg);	/* pct ranges from 1 to 2000% for shared LPARs */
	            /* maximum percentage is 2000% only on LPARs with minimum entitlement of 0.05 per virtual processor
	             * effective maximum percentage decreases when entitlement is increased, this is currently not checked for sanity */
		    if ( !(is_intpercent_ent(tmp))) {
			    printf("ERROR: -ew %s%% out of range! Allowed 1%%..2000%%\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold entitlement_warning%%=%.0f%%\n",entitlement_warning_pct); }
	    } else {
		    if ( entitlement_warning != 0 ) {
			    printf("ERROR: -ew already set to %.1f! Don't specify more than once!\n",entitlement_warning);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is floating value */
		    int tmp_int;
		    tmp_int=(int)(atof(optarg)*10); /* we use only 1 digit after the comma, remove all the others */
		    entitlement_warning=(double)tmp_int/10; /* we use only 1 digit after the comma, remove all the others */
		    if ( !(is_positive(optarg))) {
			    printf("ERROR: -ew %s out of range: Entitlement has to be >0 !\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold entitlement_warning=%.1f\n",entitlement_warning); }
	    } 
	    break;
    	case 'a':
	    /* if (verbose) { printf("Option vcpu-busy-warning with %s selected\n",optarg); } */
	    if (strstr(optarg, "%")) {
		    if ( vcpu_busy_warning_pct != 0 ) {
			    printf("ERROR: -vbw already set to %.0f%%! Don't specify more than once!\n",vcpu_busy_warning_pct);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is integer 1..100% */
		    char *tmp=optarg;
		    tmp[strlen(tmp)-1] = 0;	/* remove last char assuming it's the % */
		    vcpu_busy_warning_pct= (double)atoi(optarg);	/* pct ranges from 1 to 100% */
		    if ( !(is_intpercent(tmp))) {
			    printf("ERROR: -vbw %s%% out of range! Allowed 1%%..100%%\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold vcpu_busy_warning%%=%.0f\n",vcpu_busy_warning_pct); }
	    } else {
		    /* optarg is floating value or anything else */
		   printf("ERROR: -vbw %s out of range: Allowed 1%%..100%%\n",optarg);
		   print_usage();
		   exit(STATE_UNKNOWN);
	    } 
	    break;
    	case 'b':
	    /* if (verbose) { printf("Option vcpu-busy-critical with %s selected\n",optarg); } */
	    if (strstr(optarg, "%")) {
		    if ( vcpu_busy_critical_pct != 0 ) {
			    printf("ERROR: -vbc already set to %.0f%%! Don't specify more than once!\n",vcpu_busy_critical_pct);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is integer 1..100% */
		    char *tmp=optarg;
		    tmp[strlen(tmp)-1] = 0;	/* remove last char assuming it's the % */
		    vcpu_busy_critical_pct= (double)atoi(optarg);	/* pct ranges from 1 to 100% */
		    if ( !(is_intpercent(tmp))) {
			    printf("ERROR: -vbc %s%% out of range! Allowed 1%%..100%%\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold vcpu_busy_critical%%=%.0f\n",vcpu_busy_critical_pct); }
	    } else {
		    /* optarg is floating value or anything else */
		   printf("ERROR: -vbc %s out of range: Allowed 1%%..100%%\n",optarg);
		   print_usage();
		   exit(STATE_UNKNOWN);
	    } 
	    break;
    	case 'h':
	    print_help();
	    exit(0);
	    break;
    	case 'i':
	    /* if (verbose) { printf("Option interval with %s selected\n",optarg); } */
	    if ( is_intpos(optarg)) {
			interval = atoi (optarg);
			if ( interval > 30 ) {
				printf("ERROR: Interval out of range: %s! Allowed range is 1..30!\n",optarg);
				print_usage();
				exit(STATE_UNKNOWN);
			}
		} else {
				printf("ERROR: Invalid value for interval: %s! Allowed range is 1..30!\n",optarg);
				print_usage();
				exit(STATE_UNKNOWN);
	    }
	    break;
    	case 'x':
	    /* if (verbose) { printf("Option strict selected\n"); } */
	    strict=TRUE;
	    break;
    	case '?':
	    print_help();
	    exit(0);
	    break;

    	}
    }

    /* check if either one monitor option is used
     * you can mix as many options as you want... even if it doesn't make sense at all */
    if ( entitlement_critical+
		    entitlement_critical_pct+
		    entitlement_warning +
		    entitlement_warning_pct +
		    vcpu_busy_warning_pct +
		    vcpu_busy_critical_pct == 0 ) {
	    printf("ERROR: Specify at least on option -ew, -ec, -vbw, -vbc!\n");
	    print_usage();
	    exit(STATE_UNKNOWN);
    }

    /* 2 structures for difference calculation */
    perfstat_partition_total_t last_lparstats, lparstats;

    /* API variables and helpers */
    u_longlong_t last_time_base;
    u_longlong_t last_pcpu_user, last_pcpu_sys, last_pcpu_idle, last_pcpu_wait;

    u_longlong_t dlt_pcpu_user, dlt_pcpu_sys, dlt_pcpu_idle, dlt_pcpu_wait;
    u_longlong_t delta_time_base;
    u_longlong_t delta_purr;
    double phys_proc_consumed=0, entitlement=0, percent_ent=0, vcpu_busy=0;
    int max_entitlement=0;

    /* retrieve the logical partition metrics */
    if (!perfstat_partition_total(NULL, &last_lparstats, sizeof(perfstat_partition_total_t), 1)) {
        	printf("ENTITLEMENT UNKNOWN Error getting perfstat data from perfstat_partition_total\n");
		exit(STATE_UNKNOWN);
    }

    /* are we dedicated donating? */
    if(last_lparstats.type.b.donate_enabled)
	dedicated_donating = 1;

    /* No entitlement and pool data on dedicated LPARs */
    if ( ! dedicated_donating && ! last_lparstats.type.b.shared_enabled ) {
    	printf("ENT_POOLS UNKNOWN Entitlement and pool data not available in dedicated LPAR mode\n");
	exit(STATE_UNKNOWN);
    }

    sleep(interval);

    /* retrieve the logical partition metrics... again */
    if (!perfstat_partition_total(NULL, &lparstats, sizeof(perfstat_partition_total_t), 1)) {
        	printf("ENTITLEMENT UNKNOWN Error getting perfstat data from perfstat_partition_total\n");
		exit(STATE_UNKNOWN);
    }

    /* all last_* values were set in the previous run, the deltas is what we want */
    /* physc consists of usr+sys+wait+idle  */
    dlt_pcpu_user  = lparstats.puser - last_lparstats.puser;
    dlt_pcpu_sys   = lparstats.psys  - last_lparstats.psys;
    dlt_pcpu_idle  = lparstats.pidle - last_lparstats.pidle;
    dlt_pcpu_wait  = lparstats.pwait - last_lparstats.pwait;

    delta_purr = dlt_pcpu_user + dlt_pcpu_sys + dlt_pcpu_idle + dlt_pcpu_wait;

    /* get entitlement of lpar */
    entitlement = (double)lparstats.entitled_proc_capacity / 100.0 ;

    /* get number of virtual processors = maximum entitlement */
    max_entitlement = lparstats.online_cpus;

    /* new delta timer */
    delta_time_base = lparstats.timebase_last - last_lparstats.timebase_last;

    /* Physical Processor Consumed = Entitlement Consumed */
    phys_proc_consumed = (double)delta_purr / (double)delta_time_base;

    /* Percentage of Entitlement Consumed */
    percent_ent = (phys_proc_consumed / entitlement) * 100;

    /* Percentage of vCPU busy */ 
    vcpu_busy = (phys_proc_consumed / (double)max_entitlement) * 100;

    /* we run in shared LPAR mode? */
    if (lparstats.type.b.shared_enabled) {

	/* Compare critical and warning values */
	
	/* Entitlement checks */
	/* maximum percentage is 2000% only on LPARs with minimum entitlement of 0.05 per virtual processor
	 * effective maximum percentage decreases when entitlement is increased, this is currently not checked for sanity */
	temp_state=get_new_status("Entitlement percentage check", percent_ent, entitlement_warning_pct, entitlement_critical_pct);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* globale monitor state */
	ent_state = max_state(ent_state, temp_state);			/* entitlement monitor state */

	temp_state=get_new_status("Entitlement check", phys_proc_consumed, entitlement_warning, entitlement_critical);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* globale monitor state */
	ent_state = max_state(ent_state, temp_state);			/* entitlement monitor state */
	
	/* vCPU busy checks */
	temp_state=get_new_status("vCPU busy percentage check",vcpu_busy, vcpu_busy_warning_pct, vcpu_busy_critical_pct);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* globale monitor state */
	vcpu_busy_state = max_state(ent_state,temp_state);		/* vcpu busy monitor state */

	/* when strict checking enabled, do sanity checks too */
	if (strict) {
		if ( phys_proc_consumed == 0 ||
		     entitlement == 0
		     /* more sanity checks to think about:
		      * phys_proc_consumed > max_entitlement
		      * pool_busy_time > phys_cpus_pool
		      * shcpu_busy_time > shcpus_in_sys */
		     ) {
			if (verbose) { printf("Insane performance values detected\n" ); }
			ent_pool_state=STATE_CRITICAL;
		}

	}

	/* human readable output */
	if ( verbose ) {
		printf("\nEntitlement used: %.2f (%.2f%%), desired: %.2f, max: %.2f, vCPU busy: %6.2f%%\n",
				phys_proc_consumed,
				percent_ent,
				entitlement,
				(double)max_entitlement,
				vcpu_busy
				);

	}

	printf("ENTITLEMENT %s ent_used=%.2f(%s) ent=%.2f ent_max=%d vcpu_busy=%.2f%%(%s) |ent_used=%.2f ent=%.2f ent_max=%d vcpu_busy=%.2f\n",
			states[ent_pool_state],
			phys_proc_consumed,
			states[ent_state],
			entitlement,
			max_entitlement,
			vcpu_busy,
			states[vcpu_busy_state],
			phys_proc_consumed,
			entitlement,
			max_entitlement,
			vcpu_busy
			);

	exit(ent_pool_state);


    }
    /* what to do if we're running in dedicated donating mode:
     * phys_proc_consumed contains entitlement usage only if dedicated_donating is set
     * if dedicated_donating is not set, this monitor doesn't make sense at all */

    /* Dedicated LPAR with donating */
    else if ( dedicated_donating ) {

	/* Entitlement checks */
	/* the effective maximum percentage is 100% with dedicated donating, but you can still enter 2000% on the command-line
	 * this is currently not checked for sanity :-) */
	temp_state=get_new_status("Entitlement percentage check", percent_ent, entitlement_warning_pct, entitlement_critical_pct);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* global monitor state */
	ent_state = max_state(ent_state,temp_state);			/* entitlement monitor state */

	temp_state=get_new_status("Entitlement check", phys_proc_consumed, entitlement_warning, entitlement_critical);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* global monitor state */
	ent_state = max_state(ent_state,temp_state);			/* entitlement monitor state */

	/* vCPU busy checks */
	temp_state=get_new_status("vCPU busy percentage check", vcpu_busy, vcpu_busy_warning_pct, vcpu_busy_critical_pct);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* globale monitor state */
	vcpu_busy_state = max_state(ent_state,temp_state);		/* vcpu busy monitor state */

	/* when strict checking enabled, do sanity checks too */
	if (strict) {
		if ( phys_proc_consumed == 0 ||
		     entitlement == 0
		     ) {
			if (verbose) { printf("Insane performance values detected\n" ); }
			ent_pool_state=STATE_CRITICAL;
		}
	}

	/* human readable output */
	if ( verbose ) {
		printf("Entitlement used: %.2f (%.2f%%), desired: %.2f, max: %.2f vCPU busy: %6.2f%%\n",
				phys_proc_consumed,
				percent_ent,
				entitlement,
				(double)max_entitlement,
				vcpu_busy
				);
	}

	printf("ENTITLEMENT %s ent_used=%.2f(%s) ent=%.2f ent_max=%d vcpu_busy=%.2f%% |ent_used=%.2f ent=%.2f ent_max=%d vcpu_busy=%.2f\n",
			states[ent_pool_state],
			phys_proc_consumed,
			states[ent_state],
			entitlement,
			max_entitlement,
			vcpu_busy,
			phys_proc_consumed,
			entitlement,
			max_entitlement,
			vcpu_busy
	      		);

	exit(ent_pool_state);
    }

    /* No action when dedicated LPAR without donating... we terminated earlier already
     * just in case we get here somehow */
    printf("ENTITLEMENT UNKNOWN Unknown or unsupported LPAR mode\n");
    exit(STATE_UNKNOWN);
}

/* This is the end. */
