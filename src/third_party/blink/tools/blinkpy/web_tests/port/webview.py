# Copyright (C) 2023 Google Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
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

from blinkpy.web_tests.port import linux


class WebviewPort(linux.LinuxPort):
    port_name = 'webview'

    SUPPORTED_VERSIONS = ('webview', )
    FALLBACK_PATHS = {}
    FALLBACK_PATHS['webview'] = (
        ['webview'] + linux.LinuxPort.latest_platform_fallback_path())

    def default_expectations_files(self):
        """Returns a list of paths to expectations files that apply by default.

        There are other "test expectations" files that may be applied if
        the --additional-expectations flag is passed; those aren't included
        here.
        """
        return list(
            filter(None, [
                self.path_to_generic_test_expectations_file(),
                self._filesystem.join(self.web_tests_dir(), 'NeverFixTests'),
                self._filesystem.join(self.web_tests_dir(),
                                      'StaleTestExpectations'),
                self._filesystem.join(self.web_tests_dir(), 'SlowTests')
            ]))

    def default_child_processes(self):
        # Test against a single device by default to avoid timeouts
        return 1

    def default_smoke_test_only(self):
        # Test against selected set of tests by default to avoid timeouts
        return True

    def path_to_smoke_tests_file(self):
        return self._filesystem.join(self.web_tests_dir(), 'TestLists',
                                     'webview.filter')
