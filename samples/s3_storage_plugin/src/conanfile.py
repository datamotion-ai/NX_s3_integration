#!/usr/bin/env python3
#
# Copyright VCA Technology
#

from conans import ConanFile, CMake

class VcaCoreConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake", "virtualenv", "json"

    def requirements(self):
        # Subset of packages which currently build for ARM
        packages = [
        ]
        if "arm" not in self.settings.arch:
            packages.extend([
            ])
            if self.settings.os == 'Windows':
                pass
            elif self.settings.os == 'Linux':
                pass
        channel = 'development'
        user = 'vca'
        for package in packages:
            self.requires('{0}@{1}/{2}'.format(package, user, channel))

        # UDP recipes
        packages = [
        ]
        channel = 'development'
        user = 'udp'
        for package in packages:
            self.requires('{0}@{1}/{2}'.format(package, user, channel))

    def imports(self):
        self.copy("*.so*", dst="../out/lib", src="lib")
        self.copy("*.a", dst="../out/lib", src="lib")