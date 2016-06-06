#!/usr/bin/env python3
# Copyright (C) 2004,2008 Igor Belyi <belyi@users.sourceforge.net>
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

# Sample of unattended signing/verifying of a message.
# It uses keys for joe@example.org generated by genkey.pl script

import sys
import os
from pyme import core
from pyme.constants.sig import mode

core.check_version(None)

plain = core.Data(b"Test message")
sig = core.Data()
c = core.Context()
user = "joe"

c.signers_clear()
# Add joe@example.org's keys in the list of signers
for sigkey in c.op_keylist_all(user, 1):
    if sigkey.can_sign:
        c.signers_add(sigkey)
if not c.signers_enum(0):
    print("No secret %s's keys suitable for signing!" % user)
    sys.exit(0)

# This is a map between signer e-mail and its password
passlist = {
    b"<joe@example.org>": b"Crypt0R0cks"
    }

# callback will return password based on the e-mail listed in the hint.
c.set_passphrase_cb(lambda x,y,z: passlist[x[x.rindex("<"):]])

c.op_sign(plain, sig, mode.CLEAR)

# Print out the signature (don't forget to rewind since signing put sig at EOF)
sig.seek(0, os.SEEK_SET)
signedtext = sig.read()
sys.stdout.buffer.write(signedtext)

# Create Data with signed text.
sig2 = core.Data(signedtext)
plain2 = core.Data()

# Verify.
c.op_verify(sig2, None, plain2)
result = c.op_verify_result()

# List results for all signatures. Status equal 0 means "Ok".
for index, sign in enumerate(result.signatures):
    print("signature", index, ":")
    print("  summary:    ", sign.summary)
    print("  status:     ", sign.status)
    print("  timestamp:  ", sign.timestamp)
    print("  fingerprint:", sign.fpr)
    print("  uid:        ", c.get_key(sign.fpr, 0).uids[0].uid)

# Print "unsigned" text. Rewind since verify put plain2 at EOF.
plain2.seek(0, os.SEEK_SET)
print("\n")
sys.stdout.buffer.write(plain2.read())
