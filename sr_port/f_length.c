/****************************************************************
 *								*
 *	Copyright 2001, 2011 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"
#include "compiler.h"
#include "opcode.h"
#include "toktyp.h"
#include "advancewindow.h"

int f_length(oprtype *a, opctype op)
{
	triple *r;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	assert((OC_FNLENGTH == op) || (OC_FNZLENGTH == op));
	r = maketriple(op);
	if (EXPR_FAIL == expr(&(r->operand[0]), MUMPS_STR))
		return FALSE;
	if (TK_COMMA == TREF(window_token))
	{
		advancewindow();
		r->opcode = (OC_FNLENGTH == op) ? OC_FNPOPULATION : OC_FNZPOPULATION;      /* Not good information hiding */
		if (EXPR_FAIL == expr(&(r->operand[1]), MUMPS_STR))
			return FALSE;
	}
	ins_triple(r);
	*a = put_tref(r);
	return TRUE;
}
