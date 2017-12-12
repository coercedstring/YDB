/****************************************************************
 *								*
 * Copyright (c) 2017 YottaDB LLC. and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/* A header file for the internal uses of libyottadb that do not need to be distributed as
 * part of the user interface. Includes libyottadb.h.
 */
#ifndef LIBYOTTADB_INT_H
#define LIBYOTTADB_INT_H

#include "libyottadb.h"
#include "ydbmerrors.h"
#include "toktyp.h"
#include "funsvn.h"
#include "error.h"
#include "send_msg.h"
#include "stack_frame.h"

#define MAX_SAPI_MSTR_GC_INDX	(1 + YDB_MAX_SUBS + 1)	/* Max index in mstr array - holds varname, subs, value */

LITREF char		ctypetab[NUM_CHARS];
LITREF nametabent	svn_names[];
LITREF unsigned char	svn_index[];
LITREF svn_data_type	svn_data[];

GBLREF stack_frame	*frame_pointer;

typedef enum
{
	LYDB_RTN_GET = 1,	/* ydb_get_s() is running */
	LYDB_RTN_SET,		/* ydb_set_s() is running */
	LYDB_RTN_ZSTATUS	/* ydb_zstatus_s() is running */
} libyottadb_routines;

typedef enum
{
	LYDB_SET_INVALID = 0,	/* Varname is invalid */
	LYDB_SET_GLOBAL,	/* Setting a global variable */
	LYDB_SET_LOCAL,		/* Setting a local variable */
	LYDB_SET_ISV		/* Setting an ISV (Intrinsic Special Variable) */
} ydb_set_types;

/* Initialization macro for most simpleAPI calls */
#define LIBYOTTADB_INIT(ROUTINE)										\
MBSTART	{													\
	int	status;												\
														\
	/* A prior invocation of ydb_exit() would have set process_exiting = TRUE. Use this to disallow further	\
	 * API calls.	      	 	    	       	   		     	       	       			\
	 */    													\
	if (process_exiting)											\
	{	/* YDB runtime environment not setup/available, no driving of errors */				\
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_CALLINAFTERXIT);					\
		return YDB_ERR_CALLINAFTERXIT;									\
	}													\
	if (!gtm_startup_active || !(frame_pointer->type & SFT_CI))						\
	{	/* Have to initialize things before we can establish an error handler */			\
		if (0 != (status = ydb_init()))		/* Note - sets fgncal_stack */				\
			return -status;			   	       		    				\
		/* Since we called "ydb_init" above, "gtm_threadgbl" would have been set to a non-null VALUE	\
		 * and so any call to SETUP_THREADGBL_ACCESS done by the function that called this macro	\
		 * needs to be redone to set "lcl_gtm_threadgbl" to point to this new "gtm_threadgbl".		\
		 */												\
		SETUP_THREADGBL_ACCESS;										\
	}		       											\
	TREF(libyottadb_active_rtn) = ROUTINE;									\
	TREF(sapi_mstrs_for_gc_indx) = 0;		/* No mstrs reserved yet */				\
	ESTABLISH_NORET(ydb_simpleapi_ch, error_encountered);							\
	if (error_encountered)											\
	{													\
		REVERT;												\
		return -(TREF(ydb_error_code));									\
	}													\
	assert(MAX_LVSUBSCRIPTS == MAX_GVSUBSCRIPTS);	/* Verify so equating YDB_MAX_SUBS to MAX_LVSUBSCRIPTS	\
							 * doesn't cause us a problem.				\
							 */							\
	assert(MAX_ACTUALS <= YDB_MAX_SUBS);		/* Make sure gparam_list is big enuf */			\
} MBEND

/* Macros to promote checking an mname as being a valid mname. There are two flavors - one where
 * the first character needs to be checked and one where the first character has already been
 * checked and no further checking needs to be done.
 */

/* A macro to check the entire MNAME for validity. Returns YDB_ERR_VARNAMEINVALID otherwise */
#define VALIDATE_MNAME_C1(VARNAMESTR, VARNAMELEN)						\
MBSTART {											\
	char ctype;										\
	     											\
	ctype = ctypetab[(VARNAMESTR)[0]];							\
	switch(ctype)										\
	{											\
		case TK_LOWER:									\
		case TK_UPPER:									\
		case TK_PERCENT: 								\
			/* Valid first character */						\
			break;	       		 						\
		default:									\
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_VARNAMEINVALID);		\
	}											\
	VALIDATE_MNAME_C2(VARNAMESTR, VARNAMELEN);						\
} MBEND

/* Validate the 2nd char through the end of a given MNAME for validity returning YDB_ERR_VARNAMEINVALID otherwise */
#define VALIDATE_MNAME_C2(VARNAMESTR, VARNAMELEN)						\
MBSTART {											\
	char 		ctype, *cptr, *ctop;							\
	signed char	ch;	      								\
	       											\
	for (cptr = (VARNAMESTR), ctop = (VARNAMESTR) + (VARNAMELEN); cptr < ctop; cptr++)	\
	{	    			     	    			  		      	\
		ctype = ctypetab[*cptr];							\
		switch(ctype)									\
		{										\
			case TK_LOWER:								\
			case TK_UPPER:								\
			case TK_DIGIT:								\
				continue;							\
			default:								\
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_VARNAMEINVALID);	\
		}		       								\
	}											\
} MBEND

/* A macro for verifying an input variable name. This macro checks for:
 *   - Non-zero length
 *   - Non-NULL address
 *   - Determines the type of var (global, local, ISV)
 *
 * Any error in validation results in a return with code YDB_ERR_VARNAMEINVALID.
 */
#define VALIDATE_VARNAME(VARNAMEP, VARTYPE, VARINDEX)									\
MBSTART {														\
	char	ctype;													\
	int	index;													\
															\
	if ((0 == (VARNAMEP)->len_used) || (NULL == (VARNAMEP)->buf_addr))						\
		return YDB_ERR_VARNAMEINVALID;	  									\
	/* Characterize first char of name ($, ^, %, or letter) */							\
	ctype = ctypetab[(VARNAMEP)->buf_addr[0]];									\
	switch(ctype)													\
	{														\
		case TK_CIRCUMFLEX:											\
			if (YDB_MAX_IDENT < ((VARNAMEP)->len_used - 1))							\
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_VARNAMEINVALID);				\
			VARTYPE = LYDB_SET_GLOBAL;									\
			VALIDATE_MNAME_C1((VARNAMEP)->buf_addr + 1, (VARNAMEP)->len_used - 1);				\
			break;				      	   		      					\
		case TK_LOWER:												\
		case TK_UPPER:												\
		case TK_PERCENT: 											\
			if (YDB_MAX_IDENT < (VARNAMEP)->len_used)							\
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_VARNAMEINVALID);				\
			VARTYPE = LYDB_SET_LOCAL;									\
			VALIDATE_MNAME_C2((VARNAMEP)->buf_addr + 1, (VARNAMEP)->len_used - 1);				\
			break;												\
		case TK_DOLLAR:												\
			VARTYPE = LYDB_SET_ISV;										\
			index = namelook(svn_index, svn_names, (VARNAMEP)->buf_addr + 1, (VARNAMEP)->len_used - 1); 	\
			if (-1 == index) 	     			     						\
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_VARNAMEINVALID);				\
			if (!svn_data[index].can_set)									\
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_SVNOSET);					\
			VARINDEX = svn_data[index].opcode;								\
			break;	   											\
		default:												\
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_VARNAMEINVALID);					\
	}		       												\
} MBEND

/* Macro to validate a supplied value - can be NULL but if length is non-zero, so must the address be */
#define VALIDATE_VALUE(VARVALUE)						\
MBSTART	{									\
	if ((NULL == (VARVALUE)->buf_addr) && (0 != (VARVALUE)->len_used))	\
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_VARNAMEINVALID);	\
} MBEND

/* Macro to locate or create an entry in the symbol table for the specified base variable name */
#define FIND_BASE_VAR(VARNAMEP, VARMNAMEP, VARTABENTP, VARLVVALP)								\
MBSTART {															\
	boolean_t	added;													\
	lv_val		*lv;													\
																\
	(VARMNAMEP)->var_name.addr = (VARNAMEP)->buf_addr;	/* Load up the imbedded varname with our base var name */	\
	(VARMNAMEP)->var_name.len = (VARNAMEP)->len_used;	   	       			     	      	       		\
	s2pool(&(VARMNAMEP)->var_name);		/* Rebuffer in stringpool for protection */					\
	RECORD_MSTR_FOR_GC(&(VARMNAMEP)->var_name);										\
	(VARMNAMEP)->marked = 0;												\
	COMPUTE_HASH_MNAME((VARMNAMEP));			/* Compute its hash value */					\
	added = add_hashtab_mname_symval(&curr_symval->h_symtab, (VARMNAMEP), NULL, &(VARTABENTP));				\
	assert(VARTABENTP);													\
	if (NULL == (VARTABENTP)->value)											\
	{	    														\
		assert(added);													\
		lv_newname(tabent, curr_symval);										\
	}							 	      	  						\
	assert(NULL != LV_GET_SYMVAL((lv_val *)tabent->value));									\
	VARLVVALP = (VARTABENTP)->value;											\
} MBEND

/* Macro to set a supplied ydb_buffer_t value into an mval/lv_val */
#define SET_LVVAL_VALUE_FROM_BUFFER(LVVALP, BUFVALUE)						\
MBSTART	{											\
	((mval *)(LVVALP))->mvtype = MV_STR;							\
	((mval *)(LVVALP))->str.addr = (BUFVALUE)->buf_addr;					\
	((mval *)(LVVALP))->str.len = (BUFVALUE)->len_used;					\
} MBEND

/* Macro to pull subscripts out of caller's parameter list and buffer them for call to a runtime routine */
#define COPY_PARMS_TO_CALLG_BUFFER(COUNT, REBUFFER)										\
MBSTART	{															\
	mval		*mvalp;													\
	void		**parmp, **parmp_top;											\
				 												\
	/* Now for each subscript */												\
	VAR_START(var, varname);												\
	for (parmp = &plist.arg[1], parmp_top = parmp + (COUNT), mvalp = &plist_mvals[0]; parmp < parmp_top; parmp++, mvalp++)	\
	{	/* Pull each subscript descriptor out of param list and put in our parameter buffer */	    	     		\
		subval = va_arg(var, ydb_buffer_t *);	       	    	       	   	     	    				\
		if (NULL != subval)		  										\
		{	/* A subscript has been specified - copy it to the associated mval and put its address			\
			 * in the param list to pass to op_putindx()								\
			 */													\
			SET_LVVAL_VALUE_FROM_BUFFER(mvalp, subval);								\
			if (REBUFFER)												\
			{													\
				s2pool(&(mvalp->str));	/* Rebuffer in stringpool for protection */				\
				RECORD_MSTR_FOR_GC(&(mvalp->str));								\
			}													\
		} else					   									\
		{	/* No subscript specified - error */									\
			va_end(var);		    	  									\
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_VARNAMEINVALID);						\
		}				    		 								\
		*parmp = mvalp;													\
	}	       	 													\
	plist.n = (COUNT) + 1;			/* Bump to include varname in parms */						\
	va_end(var);														\
} MBEND

/* Macro to record the address of a given mstr so garbage collection knows where to find it to fix it up if needbe */
#define RECORD_MSTR_FOR_GC(MSTRP)										\
MBSTART	{													\
	mstr	*mstrp;												\
														\
	if (NULL == (mstrp = TREF(sapi_mstrs_for_gc_ary)))	/* Note assignment */				\
	{													\
		mstrp = TREF(sapi_mstrs_for_gc_ary) = malloc(SIZEOF(mstr) * (MAX_SAPI_MSTR_GC_INDX + 1));	\
		TREF(sapi_mstrs_for_gc_indx) = 0;			    					\
	}				       									\
	assert(MAX_SAPI_MSTR_GC_INDX > TREF(sapi_mstrs_for_gc_indx));						\
	if (0 < (MSTRP)->len)											\
		TAREF1(sapi_mstrs_for_gc_ary, (TREF(sapi_mstrs_for_gc_indx))++) = MSTRP;			\
} MBEND
#endif /*  LIBYOTTADB_INT_H */
