/* export.c -  encrypt functions
 *	Copyright (C) 2000 Werner Koch (dd9jn)
 *      Copyright (C) 2001, 2002 g10 Code GmbH
 *
 * This file is part of GPGME.
 *
 * GPGME is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GPGME is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "util.h"
#include "context.h"
#include "ops.h"


static void
export_status_handler (GpgmeCtx ctx, GpgStatusCode code, char *args)
{
  if (ctx->error)
    return;

  DEBUG2 ("export_status: code=%d args=`%s'\n", code, args);
  /* FIXME: Need to do more */
}


static GpgmeError
_gpgme_op_export_start (GpgmeCtx ctx, int synchronous,
			GpgmeRecipients recp, GpgmeData keydata)
{
  GpgmeError err = 0;

  err = _gpgme_op_reset (ctx, synchronous);
  if (err)
    goto leave;

  if (!keydata || gpgme_data_get_type (keydata) != GPGME_DATA_TYPE_NONE)
    {
      err = mk_error (Invalid_Value);
      goto leave;
    }
  _gpgme_data_set_mode (keydata, GPGME_DATA_MODE_IN);

  _gpgme_engine_set_status_handler (ctx->engine, export_status_handler, ctx);
  _gpgme_engine_set_verbosity (ctx->engine, ctx->verbosity);

  err = _gpgme_engine_op_export (ctx->engine, recp, keydata, ctx->use_armor);
  if (!err)
    err = _gpgme_engine_start (ctx->engine, ctx);

 leave:
  if (err)
    {
      ctx->pending = 0; 
      _gpgme_engine_release (ctx->engine);
      ctx->engine = NULL;
    }
  return err;
}

GpgmeError
gpgme_op_export_start (GpgmeCtx ctx, GpgmeRecipients recp, GpgmeData keydata)
{
  return _gpgme_op_export_start (ctx, 0, recp, keydata);
}

/**
 * gpgme_op_export:
 * @c: the context
 * @recp: a list of recipients or NULL
 * @keydata: Returns the keys
 * 
 * This function can be used to extract public keys from the GnuPG key
 * database either in armored (by using gpgme_set_armor()) or in plain
 * binary form.  The function expects a list of user IDs in @recp for
 * whom the public keys are to be exported.
 * 
 * Return value: 0 for success or an error code
 **/
GpgmeError
gpgme_op_export (GpgmeCtx ctx, GpgmeRecipients recipients, GpgmeData keydata)
{
  GpgmeError err = _gpgme_op_export_start (ctx, 1, recipients, keydata);
  if (!err)
    {
      err = _gpgme_wait_one (ctx);
      /* XXX We don't get status information.  */
      if (!ctx->error && gpgme_data_get_type (keydata) == GPGME_DATA_TYPE_NONE)
	ctx->error = mk_error (No_Recipients);
      err = ctx->error;
    }
  return err;
}
