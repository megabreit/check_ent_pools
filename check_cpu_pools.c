/*
 * monitors pool usage (system and other shared pools) on shared processor LPARs
 * not usable on dedicated LPARs
 * compiles with IBM xlC and gcc on AIX 5.3(>TL6), 6.1 and 7.1
 * 
 * Compile with: cc -o check_cpu_pools -lperfstat check_cpu_pools.c
 *
 * This is stripped down from check_ent_pools.c, entitlement parts removed
 *
 * This nagios plugin comes with ABSOLUTELY NO WARRANTY. You may redistribute
 * copies of the plugin under the terms of the GNU General Public License.
 * For more information about these matters, see the file named COPYING.
*/
const char *progname = "check_cpu_pools";
const char *program_name = "check_cpu_pools";
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
int ent_state=STATE_OK;
int pool_state=STATE_OK, pool_free_state=STATE_OK;
int syspool_state=STATE_OK, syspool_free_state=STATE_OK;

int dedicated_donating=0;	/* marker for dedicated donating mode */
int interval=1;			/* default interval in seconds between the 2 perflib calls = monitoring period */
int verbose=FALSE;		/* only 1 verbose level... violating the plugin recommendations here */
int strict=FALSE;		/* additional sanity checking of various system values */
int pool_check_requested=0;	/* indicator for pool check requests, to react properly when there are no pools to check */

/* monitoring variables */
double pool_critical_pct=0, pool_warning_pct=0;
double pool_critical=0, pool_warning=0;
double pool_free_critical_pct=0, pool_free_warning_pct=0;
double pool_free_critical=0, pool_free_warning=0;
double system_critical_pct=0, system_warning_pct=0;
double system_critical=0, system_warning=0;
double system_free_critical_pct=0, system_free_warning_pct=0;
double system_free_critical=0, system_free_warning=0;


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
 	printf (" %s [ -pw=limit ] [ -pc=limit ]\n", progname);
 	printf ("     [ -pfw=limit ] [ -pfc=limit ] [ -sw=limit ] [ -sc=limit ]\n");
 	printf ("     [ -sfw=limit ] [ -sfc=limit ] [ -strict ] [ -i=interval ] [ -h ] [ -v ] [ -V ]\n\n");
}

void print_help (void)
{
	printf ("%s %s\n",progname, version);

	printf ("Copyright (c) %s %s <%s>\n",copyright,name,email);

	printf ("%s\n", _("This plugin checks the CPU pool processor utilization on"));
	printf ("%s\n", _("AIX shared processor or dedicated donating LPARs and generates an alert if"));
	printf ("%s\n", _("values are out of the threshold limits"));

	printf ("\n\n");

	print_usage ();

	printf (" %s\n", "Pool monitors (only available on shared processor LPARs):");

	printf (" %s\n", "-pw, --pool-warning=VALUE");
	printf ("    %s\n", _("Exit with WARNING status if pool usage is higher than VALUE"));
	printf (" %s\n", "-pw, --pool-warning=PERCENT%");
	printf ("    %s\n", _("Exit with WARNING status if pool usage is higher than PERCENT of defined"));
	printf ("    %s\n", _("entitled capacity"));
	printf (" %s\n", "-pc, --pool-critical=VALUE");
	printf ("    %s\n", _("Exit with CRITICAL status if pool usage is higher than VALUE"));
	printf (" %s\n", "-pc, --pool-critical=PERCENT%");
	printf ("    %s\n", _("Exit with CRITICAL status if pool usage is higher than PERCENT of defined"));
	printf ("    %s\n", _("entitled capacity"));

	printf (" %s\n", "-pfw, --pool-free-warning=VALUE");
	printf ("    %s\n", _("Exit with WARNING status if pool usage is higher than VALUE"));
	printf (" %s\n", "-pfw, --pool-free-warning=PERCENT%");
	printf ("    %s\n", _("Exit with WARNING status if pool usage is higher than PERCENT of defined"));
	printf ("    %s\n", _("entitled capacity"));
	printf (" %s\n", "-pfc, --pool-free-critical=VALUE");
	printf ("    %s\n", _("Exit with CRITICAL status if pool usage is higher than VALUE"));
	printf (" %s\n", "-pfc, --pool-free-critical=PERCENT%");
	printf ("    %s\n", _("Exit with CRITICAL status if pool usage is higher than PERCENT of defined"));
	printf ("    %s\n", _("entitled capacity"));

	printf (" %s\n", "-sw, --system-warning=VALUE");
	printf ("    %s\n", _("Exit with WARNING status if system pool usage is higher than VALUE"));
	printf (" %s\n", "-sw, --system-warning=PERCENT%");
	printf ("    %s\n", _("Exit with WARNING status if system pool usage is higher than PERCENT"));
        printf ("    %s\n", _("of defined entitled capacity"));
	printf (" %s\n", "-sc, --system-critical=VALUE");
	printf ("    %s\n", _("Exit with CRITICAL status if system pool usage is higher than VALUE"));
	printf (" %s\n", "-sc, --system-critical=PERCENT%");
	printf ("    %s\n", _("Exit with CRITICAL status if system pool usage is higher than PERCENT"));
	printf ("    %s\n", _("of defined entitled capacity"));

	printf (" %s\n", "-sfw, --system-free-warning=VALUE");
	printf ("    %s\n", _("Exit with WARNING status if number of free system pool cpus is lower"));
	printf ("    %s\n", _("than VALUE"));
	printf (" %s\n", "-sfw, --system-free-warning=PERCENT%");
	printf ("    %s\n", _("Exit with WARNING status if number of free system pool cpus is lower"));
	printf ("    %s\n", _("than PERCENT of available cpus"));
	printf (" %s\n", "-sfc, --system-free-critical=VALUE");
	printf ("    %s\n", _("Exit with CRITICAL status if number of free system pool cpus is lower"));
        printf ("    %s\n", _("than VALUE"));
	printf (" %s\n", "-sfc, --system-free-critical=PERCENT%");
	printf ("    %s\n", _("Exit with CRITICAL status if number of free system pool cpus is lower"));
	printf ("    %s\n", _("than PERCENT of available cpus"));
	printf ("\n");
	printf (" %s\n", "-i, --interval=INTEGER");
	printf ("    %s\n", _("measurement interval in INTEGER seconds (1..30). Default is 1"));
	printf (" %s\n", "-x, -strict, --strict");
	printf ("    %s\n", _("Exit with CRITICAL status if pool values are obviously wrong"));
	printf ("    %s\n", _("e.g. pool sizes or usage values are 0, number of pool cpus is higher than"));
        printf ("    %s\n", _("installed cpus is 0"));
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
	printf ("\n");
	printf ("%s\n", _("Warning and critical checks can be configured independently, critical checks"));
        printf ("%s\n", _("without warnings are possible"));
	printf ("%s\n", _("Exactly one PERCENT and one VALUE for each option is possible, see examples"));
	printf ("\n");
	printf ("%s\n", _("Examples:"));
	printf ("\n");
	printf ("%s\n", _("Checks current pool usage for >20 CPUs, >95% and <1 free pool CPU and generate"));
        printf ("%s\n", _("critical events only:"));
	printf ("%s\n", _("check_cpu_pools -pc 20 -pc 95% -pfc 1"));
	printf ("\n");
	printf ("%s\n", _("Checks current system pool usage for >20, >24 CPUs and <2.5, <1 or 98% free"));
	printf ("%s\n", _("system pool CPU:"));
	printf ("%s\n", _("check_cpu_pools -sw 20 -sc 24 -sfw 2.5 -sfc 1 -sfc 98%"));

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
	{"pc",                   required_argument, 0, 'p'},
	{"pool-critical",        required_argument, 0, 'p'},
	{"pw",                   required_argument, 0, 'q'},
	{"pool-warning",         required_argument, 0, 'q'},
	{"sc",                   required_argument, 0, 't'},
	{"system-critical",      required_argument, 0, 't'},
	{"sw",                   required_argument, 0, 's'},
	{"system-warning",       required_argument, 0, 's'},
	{"sfc",                  required_argument, 0, 'g'},
	{"system-free-critical", required_argument, 0, 'g'},
	{"sfw",                  required_argument, 0, 'k'},
	{"system-free-warning",  required_argument, 0, 'k'},
	{"pfc",                  required_argument, 0, 'l'},
	{"pool-free-critical",   required_argument, 0, 'l'},
	{"pfw",                  required_argument, 0, 'm'},
	{"pool-free-warning",    required_argument, 0, 'm'},
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
    	case 'p':
	    /* if (verbose) { printf("Option pool-critical with %s selected\n",optarg); } */
	    if (strstr(optarg, "%")) {
		    if ( pool_critical_pct != 0 ) {
			    printf("ERROR: -pc already set to %.0f%%! Don't specify more than once!\n",pool_critical_pct);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is integer 1..100% */
		    char *tmp=optarg;
		    tmp[strlen(tmp)-1] = 0;	/* remove last char assuming it's the % */
		    pool_critical_pct= (double)atoi(optarg);	/* pct ranges from 1 to 100% */
		    pool_check_requested+=1;      /* to exit easily later if lpar mode is dedicated donating */
		    if ( !(is_intpercent(tmp))) {
			    printf("ERROR: -pc %s%% out of range! Allowed 1%%..100%%\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold pool_critical%%=%.0f\n",pool_critical_pct); }
	    } else {
		    if ( pool_critical != 0 ) {
			    printf("ERROR: -pc already set to %.1f! Don't specify more than once!\n",pool_critical);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is floating value */
		    int tmp_int;
		    tmp_int=(int)(atof(optarg)*10); /* we use only 1 digit after the comma, remove all the others */
		    pool_critical=(double)tmp_int/10; /* we use only 1 digit after the comma, remove all the others */
		    pool_check_requested+=1;
		    if ( !(is_positive(optarg))) {
			    printf("ERROR: -pc %s out of range: Argument has to be >0 !\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold pool_critical=%.1f\n",pool_critical); }
	    } 
	    break;
    	case 'q':
	    /* if (verbose) { printf("Option pool-warning with %s selected\n",optarg); } */
	    if (strstr(optarg, "%")) {
		    if ( pool_warning_pct != 0 ) {
			    printf("ERROR: -pw already set to %.0f%%! Don't specify more than once!\n",pool_warning_pct);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is integer 1..100% */
		    char *tmp=optarg;
		    tmp[strlen(tmp)-1] = 0;	/* remove last char assuming it's the % */
		    pool_warning_pct= (double)atoi(optarg);	/* pct ranges from 1 to 100% */
		    pool_check_requested+=1;
		    if ( !(is_intpercent(tmp))) {
			    printf("ERROR: -pw %s%% out of range! Allowed 1%%..100%%\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold pool_warning%%=%.0f\n",pool_warning_pct); }
	    } else {
		    if ( pool_warning != 0 ) {
			    printf("ERROR: -pw already set to %.1f! Don't specify more than once!\n",pool_warning);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is floating value */
		    int tmp_int;
		    tmp_int=(int)(atof(optarg)*10); /* we use only 1 digit after the comma, remove all the others */
		    pool_warning=(double)tmp_int/10; /* we use only 1 digit after the comma, remove all the others */
		    pool_check_requested+=1;
		    if ( !(is_positive(optarg))) {
			    printf("ERROR: -pw %s out of range: Argument has to be >0 !\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold pool_warning=%.1f\n",pool_warning); }
	    } 
	    break;
    	case 'l':
	    /* if (verbose) { printf("Option pool-free-critical with %s selected\n",optarg); } */
	    if (strstr(optarg, "%")) {
		    if ( pool_free_critical_pct != 0 ) {
			    printf("ERROR: -pfc already set to %.0f%%! Don't specify more than once!\n",pool_free_critical_pct);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is integer 1..100% */
		    char *tmp=optarg;
		    tmp[strlen(tmp)-1] = 0;	/* remove last char assuming it's the % */
		    pool_free_critical_pct= (double)atoi(optarg);	/* pct ranges from 1 to 100% */
		    pool_check_requested+=1;
		    if ( !(is_intpercent(tmp))) {
			    printf("ERROR: -pfc %s%% out of range! Allowed 1%%..100%%\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold pool_free_critical%%=%.0f\n",pool_free_critical_pct); }
	    } else {
		    if ( pool_free_critical != 0 ) {
			    printf("ERROR: -pfc already set to %.1f! Don't specify more than once!\n",pool_free_critical);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is floating value */
		    int tmp_int;
		    tmp_int=(int)(atof(optarg)*10); /* we use only 1 digit after the comma, remove all the others */
		    pool_free_critical=(double)tmp_int/10; /* we use only 1 digit after the comma, remove all the others */
		    pool_check_requested+=1;
		    if ( !(is_positive(optarg))) {
			    printf("ERROR: -pfc %s out of range: Argument has to be >0 !\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold pool_free_critical=%.1f\n",pool_free_critical); }
	    } 
	    break;
    	case 'm':
	    /* if (verbose) { printf("Option pool-free-warning with %s selected\n",optarg); } */
	    if (strstr(optarg, "%")) {
		    if ( pool_free_warning_pct != 0 ) {
			    printf("ERROR: -pfw already set to %.0f%%! Don't specify more than once!\n",pool_free_warning_pct);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is integer 1..100% */
		    char *tmp=optarg;
		    tmp[strlen(tmp)-1] = 0;	/* remove last char assuming it's the % */
		    pool_free_warning_pct= (double)atoi(optarg);	/* pct ranges from 1 to 100% */
		    pool_check_requested+=1;
		    if ( !(is_intpercent(tmp))) {
			    printf("ERROR: -pfw %s%% out of range! Allowed 1%%..100%%\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold pool_free_warning%%=%.0f\n",pool_free_warning_pct); }
	    } else {
		    if ( pool_free_warning != 0 ) {
			    printf("ERROR: -pfw already set to %.1f! Don't specify more than once!\n",pool_free_warning);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is floating value */
		    int tmp_int;
		    tmp_int=(int)(atof(optarg)*10); /* we use only 1 digit after the comma, remove all the others */
		    pool_free_warning=(double)tmp_int/10; /* we use only 1 digit after the comma, remove all the others */
		    pool_check_requested+=1;
		    if ( !(is_positive(optarg))) {
			    printf("ERROR: -pfw %s out of range: Argument has to be >0 !\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold pool_free_warning=%.1f\n",pool_free_warning); }
	    } 
	    break;
    	case 's':
	    /* if (verbose) { printf("Option system-warning with %s selected\n",optarg); } */
	    if (strstr(optarg, "%")) {
		    if ( system_warning_pct != 0 ) {
			    printf("ERROR: -sw already set to %.0f%%! Don't specify more than once!\n",system_warning_pct);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is integer 1..100% */
		    char *tmp=optarg;
		    tmp[strlen(tmp)-1] = 0;	/* remove last char assuming it's the % */
		    system_warning_pct= (double)atoi(optarg);	/* pct ranges from 1 to 100% */
		    pool_check_requested+=1;
		    if ( !(is_intpercent(tmp))) {
			    printf("ERROR: -sw %s%% out of range! Allowed 1%%..100%%\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold system_warning%%=%.0f\n",system_warning_pct); }
	    } else {
		    if ( system_warning != 0 ) {
			    printf("ERROR: -sw already set to %.1f! Don't specify more than once!\n",system_warning);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is floating value */
		    int tmp_int;
		    tmp_int=(int)(atof(optarg)*10); /* we use only 1 digit after the comma, remove all the others */
		    system_warning=(double)tmp_int/10; /* we use only 1 digit after the comma, remove all the others */
		    pool_check_requested+=1;
		    if ( !(is_positive(optarg))) {
			    printf("ERROR: -sw %s out of range: Argument has to be >0 !\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold system_warning=%.1f\n",system_warning); }
	    } 
	    break;
    	case 't':
	    /* if (verbose) { printf("Option system-critical with %s selected\n",optarg); } */
	    if (strstr(optarg, "%")) {
		    if ( system_critical_pct != 0 ) {
			    printf("ERROR: -sc already set to %.0f%%! Don't specify more than once!\n",system_critical_pct);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is integer 1..100% */
		    char *tmp=optarg;
		    tmp[strlen(tmp)-1] = 0;	/* remove last char assuming it's the % */
		    system_critical_pct= (double)atoi(optarg);	/* pct ranges from 1 to 100% */
		    pool_check_requested+=1;
		    if ( !(is_intpercent(tmp))) {
			    printf("ERROR: -sc %s%% out of range! Allowed 1%%..100%%\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold system_critical%%=%.0f\n",system_critical_pct); }
	    } else {
		    if ( system_critical != 0 ) {
			    printf("ERROR: -sc set to %.1f! Don't specify more than once!\n",system_critical);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is floating value */
		    int tmp_int;
		    tmp_int=(int)(atof(optarg)*10); /* we use only 1 digit after the comma, remove all the others */
		    system_critical=(double)tmp_int/10; /* we use only 1 digit after the comma, remove all the others */
		    pool_check_requested+=1;
		    if ( !(is_positive(optarg))) {
			    printf("ERROR: -sc %s out of range: Argument has to be >0 !\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold system_critical=%.1f\n",system_critical); }
	    } 
	    break;
    	case 'g':
	    /* if (verbose) { printf("Option system-free-critical with %s selected\n",optarg); } */
	    if (strstr(optarg, "%")) {
		    if ( system_free_critical_pct != 0 ) {
			    printf("ERROR: -sfc already set to %.0f%%! Don't specify more than once!\n",system_free_critical_pct);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is integer 1..100% */
		    char *tmp=optarg;
		    tmp[strlen(tmp)-1] = 0;	/* remove last char assuming it's the % */
		    system_free_critical_pct= (double)atoi(optarg);	/* pct ranges from 1 to 100% */
		    pool_check_requested+=1;
		    if ( !(is_intpercent(tmp))) {
			    printf("ERROR: Argument %s%% out of range! Allowed 1%%..100%%\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold system_free_critical%%=%.0f\n",system_free_critical_pct); }
	    } else {
		    if ( system_free_critical != 0 ) {
			    printf("ERROR: -sfc already set to %.1f! Don't specify more than once!\n",system_free_critical);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is floating value */
		    int tmp_int;
		    tmp_int=(int)(atof(optarg)*10); /* we use only 1 digit after the comma, remove all the others */
		    system_free_critical=(double)tmp_int/10; /* we use only 1 digit after the comma, remove all the others */
		    pool_check_requested+=1;
		    if ( !(is_positive(optarg))) {
			    printf("ERROR: -sfc %s out of range: Argument has to be >0 !\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold system_free_critical=%.1f\n",system_free_critical); }
	    } 
	    break;
    	case 'k':
	    /* if (verbose) { printf("Option system-free-warning with %s selected\n",optarg); } */
	    if (strstr(optarg, "%")) {
		    if ( system_free_warning_pct != 0 ) {
			    printf("ERROR: -sfw already set to %.0f%%! Don't specify more than once!\n",system_free_warning_pct);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is integer 1..100% */
		    char *tmp=optarg;
		    tmp[strlen(tmp)-1] = 0;	/* remove last char assuming it's the % */
		    system_free_warning_pct= (double)atoi(optarg);	/* pct ranges from 1 to 100% */
		    pool_check_requested+=1;
		    if ( !(is_intpercent(tmp))) {
			    printf("ERROR: -sfw %s%% out of range! Allowed 1%%..100%%\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold system_free_warning%%=%.0f\n",system_free_warning_pct); }
	    } else {
		    if ( system_free_warning != 0 ) {
			    printf("ERROR: -sfw already set to %.1f! Don't specify more than once!\n",system_free_warning);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    /* optarg is floating value */
		    int tmp_int;
		    tmp_int=(int)(atof(optarg)*10); /* we use only 1 digit after the comma, remove all the others */
		    system_free_warning=(double)tmp_int/10; /* we use only 1 digit after the comma, remove all the others */
		    pool_check_requested+=1;
		    if ( !(is_positive(optarg))) {
			    printf("ERROR: -sfw %s out of range: Argument has to be >0 !\n",optarg);
			    print_usage();
			    exit(STATE_UNKNOWN);
		    }
		    if (verbose) { printf("threshold system_free_warning=%.1f\n",system_free_warning); }
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
    if ( pool_critical + pool_warning +
		    pool_critical_pct + pool_warning_pct +
		    pool_free_critical + pool_free_warning +
		    pool_free_critical_pct + pool_free_warning_pct +
		    system_critical + system_warning +
		    system_critical_pct + system_warning_pct +
		    system_free_critical + system_free_warning +
		    system_free_critical_pct + system_free_warning_pct == 0 ) {
	    printf("ERROR: Specify at least on option -pw, -pc, -pfw, -pfc, -sw, -sc, -sfw, -sfc!\n");
	    print_usage();
	    exit(STATE_UNKNOWN);
    }

    /* 2 structures for difference calculation */
    perfstat_partition_total_t last_lparstats, lparstats;

    /* API variables and helpers */
    u_longlong_t last_time_base;
    u_longlong_t last_pool_busy_time, last_shcpu_busy_time;
    u_longlong_t last_pool_idle_time = 0;

    u_longlong_t dlt_pool_busy_time, dlt_shcpu_busy_time;
    u_longlong_t delta_time_base;
    u_longlong_t shcpus_in_sys=0;
    double pool_busy_time=0, pool_busy_time_pct=0, shcpu_busy_time=0, shcpu_busy_time_pct=0;
    double shcpu_free_time=0, shcpu_free_time_pct=0;
    double pool_free_time=0, pool_free_time_pct=0;
    int pool_id;
    int phys_cpus_pool=0;

    /* retrieve the logical partition metrics */
    if (!perfstat_partition_total(NULL, &last_lparstats, sizeof(perfstat_partition_total_t), 1)) {
        	printf("CPU_POOLS UNKNOWN Error getting perfstat data from perfstat_partition_total\n");
		exit(STATE_UNKNOWN);
    }

    /* Bail out when any pool monitoring is requested and there is no
     * pool authority enabled (HMC -> LPAR-name -> Enable performance collection
     * or from hmc command line: chsyscfg -m msys -r LPAR  -i "LPAR _id=XX,allow_perf_collection=1" */
    if (pool_check_requested && last_lparstats.type.b.shared_enabled && !last_lparstats.type.b.pool_util_authority) {
	    printf("CPU_POOLS CRITICAL Performance collection is disabled in LPAR profile! Monitoring is not possible!\n");
	    exit(STATE_CRITICAL);
    }

    /* are we dedicated donating? */
    if(last_lparstats.type.b.donate_enabled)
	dedicated_donating = 1;

    /* No entitlement and pool data on dedicated LPARs */
    if ( ! dedicated_donating && ! last_lparstats.type.b.shared_enabled ) {
    	printf("CPU_POOLS UNKNOWN Entitlement and pool data not available in dedicated LPAR mode\n");
	exit(STATE_UNKNOWN);
    }
    /* If we run in donating, there is no pool or system pool data at all, only entitlement data
     * -> exit with UNKNOWN when still a pool monitoring is requested:
     * */
    if ( dedicated_donating && pool_check_requested > 0 ) {
	printf("CPU_POOLS UNKNOWN Pool data is not available in dedicated donating mode!\n");
	exit(STATE_UNKNOWN);
    }

    sleep(interval);

    /* retrieve the logical partition metrics... again */
    if (!perfstat_partition_total(NULL, &lparstats, sizeof(perfstat_partition_total_t), 1)) {
        	printf("CPU_POOLS UNKNOWN Error getting perfstat data from perfstat_partition_total\n");
		exit(STATE_UNKNOWN);
    }
    /* delta pool usage values */
    dlt_pool_busy_time = lparstats.pool_busy_time - last_lparstats.pool_busy_time;
    dlt_shcpu_busy_time = lparstats.shcpu_busy_time - last_lparstats.shcpu_busy_time;

    /* get pool sizes */
    phys_cpus_pool = lparstats.phys_cpus_pool;
    shcpus_in_sys = lparstats.shcpus_in_sys;

    /* pool id of this lpar */
    pool_id = lparstats.pool_id;

    /* new delta timer */
    delta_time_base = lparstats.timebase_last - last_lparstats.timebase_last;

    /* we run in shared LPAR mode? */
    if (lparstats.type.b.shared_enabled) {

        /* Shared LPAR with pool authority enabled -> we have pool data */
        if (lparstats.type.b.pool_util_authority) {
        /* Available Pool Processor (app) */
	   pool_free_time=(double)(lparstats.pool_idle_time - last_lparstats.pool_idle_time) / (XINTFRAC*(double)delta_time_base);
	   pool_free_time_pct=pool_free_time*100/phys_cpus_pool;

           /* busy CPUs in Pool = phys_cpus_pool - app */
	   pool_busy_time=(double)dlt_pool_busy_time/(XINTFRAC*(double)delta_time_base);
	   pool_busy_time_pct= pool_busy_time * 100 / phys_cpus_pool;

           /* busy CPUs in managed system = Shared Pool 0 usage */
	   shcpu_busy_time=(double)dlt_shcpu_busy_time/(XINTFRAC*(double)delta_time_base);
	   shcpu_busy_time_pct=shcpu_busy_time * 100 / (double)shcpus_in_sys;
	   /* free CPUs in managed system = busy CPUs - shcpus_in_sys */
	   shcpu_free_time=shcpus_in_sys - shcpu_busy_time;
	   shcpu_free_time_pct=shcpu_free_time * 100 / (double)shcpus_in_sys;
        }

	/* Compare critical and warning values */
	
	/* Pool usage */
	temp_state=get_new_status("Pool usage percentage check", pool_busy_time_pct, pool_warning_pct, pool_critical_pct);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* globale monitor state */
	pool_state = max_state(pool_state, temp_state);			/* pool monitor state */

	temp_state=get_new_status("Pool usage check", pool_busy_time, pool_warning, pool_critical);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* globale monitor state */
	pool_state = max_state(pool_state, temp_state);			/* pool monitor state */
	
	/* Pool free */
	temp_state=get_new_lower_status("Pool free percentage check", pool_free_time_pct, pool_free_warning_pct, pool_free_critical_pct);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* globale monitor state */
	pool_free_state = max_state(pool_free_state, temp_state);	/* pool free monitor state */

	temp_state=get_new_lower_status("Pool free check", pool_free_time, pool_free_warning, pool_free_critical);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* globale monitor state */
	pool_free_state = max_state(pool_free_state, temp_state);	/* pool free monitor state */

	/* System pool usage */
	temp_state=get_new_status("System pool usage percentage check", shcpu_busy_time_pct, system_warning_pct, system_critical_pct);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* globale monitor state */
	syspool_state = max_state(syspool_state, temp_state);		/* system pool state */

	temp_state=get_new_status("System pool usage check", shcpu_busy_time, system_warning, system_critical);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* globale monitor state */
	syspool_state = max_state(syspool_state, temp_state);		/* system pool state */

	/* System pool free */
	temp_state=get_new_lower_status("System pool free percentage check", shcpu_free_time_pct, system_free_warning_pct, system_free_critical_pct);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* globale monitor state */
	syspool_free_state = max_state(syspool_free_state, temp_state);	/* system pool free state */

	temp_state=get_new_lower_status("System pool free check", shcpu_free_time, system_free_warning, system_free_critical);

	ent_pool_state = max_state(ent_pool_state, temp_state);		/* globale monitor state */
	syspool_free_state = max_state(syspool_free_state, temp_state);	/* system pool free state */

	/* when strict checking enabled, do sanity checks too */
	if (strict) {
		if ( phys_cpus_pool == 0 ||
		     pool_busy_time == 0 ||
		     shcpus_in_sys == 0 ||
		     shcpu_busy_time == 0 ||
		     phys_cpus_pool > shcpus_in_sys
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
		printf("Pool ID %3d size: %4d, used: %6.2f (%6.2f%%), free: %6.2f (%6.2f%%) \n",
				pool_id,
				phys_cpus_pool,
				pool_busy_time,
				pool_busy_time_pct,
				pool_free_time,
				pool_free_time_pct
		      		);
		printf("System pool size: %4llu, used: %6.2f (%6.2f%%), free: %6.2f (%6.2f%%)\n",
				shcpus_in_sys,
				shcpu_busy_time,
				shcpu_busy_time_pct,
				shcpu_free_time,
				shcpu_free_time_pct
		      		);


	}

	printf("CPU_POOLS %s pool_id=%d pool_size=%d pool_used=%.2f(%s) pool_free=%.2f(%s) syspool_size=%llu syspool_used=%.2f(%s) syspool_free=%.2f(%s) |pool_id=%d pool_size=%d pool_used=%.2f pool_free=%.2f syspool_size=%llu syspool_used=%.2f syspool_free=%.2f\n",
			states[ent_pool_state],
			pool_id,
			phys_cpus_pool,
			pool_busy_time,
			states[pool_state],
			pool_free_time,
			states[pool_free_state],
			shcpus_in_sys,
			shcpu_busy_time,
			states[syspool_state],
			shcpu_free_time,
			states[syspool_free_state],
			pool_id,
			phys_cpus_pool,
			pool_busy_time,
			pool_free_time,
			shcpus_in_sys,
			shcpu_busy_time,
			shcpu_free_time
			);

	exit(ent_pool_state);


    }
    /* what to do if we're running in dedicated donating mode:
     * phys_proc_consumed contains entitlement usage only if dedicated_donating is set
     * if dedicated_donating is not set, this monitor doesn't make sense at all */

    /* Dedicated LPAR with donating */
    else if ( dedicated_donating ) {

	printf("CPU_POOLS UNKNOWN No pool data available in dedicated donating LPAR mode!\n");

	exit(STATE_UNKNOWN);
	}

    /* No action when dedicated LPAR without donating... we terminated earlier already
     * just in case we get here somehow */
    printf("CPU_POOLS UNKNOWN Unknown or unsupported LPAR mode\n");
    exit(STATE_UNKNOWN);
}

/* This is the end. */
