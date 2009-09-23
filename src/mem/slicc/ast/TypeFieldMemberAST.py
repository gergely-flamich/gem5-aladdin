# Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
# Copyright (c) 2009 The Hewlett-Packard Development Company
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from slicc.ast.TypeFieldAST import TypeFieldAST

class TypeFieldMemberAST(TypeFieldAST):
    def __init__(self, slicc, type_ast, field_id, pairs, rvalue):
        super(TypeFieldMemberAST, self).__init__(slicc, pairs)

        self.type_ast = type_ast
        self.field_id = field_id
        self.rvalue = rvalue

    def __repr__(self):
        return "[TypeFieldMember: %r]" % self.field_id

    def generate(self, type):
        # Lookup type
        field_type = self.type_ast.type

        # check type if this is a initialization
        init_code = ""
        if self.rvalue:
            rvalue_type,init_code = self.rvalue.inline(True)
            if field_type != rvalue_type:
                self.error("Initialization type mismatch '%s' and '%s'" % \
                           (field_type, rvalue_type))

        # Add data member to the parent type
        if not type.dataMemberAdd(self.field_id, field_type, self.pairs,
                                  init_code):

            error("Duplicate data member: %s:%s" % (type_ptr, field_id))
