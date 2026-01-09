from conan import ConanFile
from conan.tools.cmake import cmake_layout


class RmwRobotopsConan(ConanFile):
    name = 'rmw_robotops'
    settings = 'os', 'compiler', 'build_type', 'arch'
    generators = 'CMakeDeps', 'CMakeToolchain'

    def requirements(self):
        self.requires('robotops-config/0.3.6')

    def layout(self):
        cmake_layout(self)

    def configure(self):
        # This is a ROS2 package, we only use conan for specific dependencies
        # like robotops-config, not for the entire build
        pass
