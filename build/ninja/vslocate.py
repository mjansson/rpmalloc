#!/usr/bin/env python

"""Locate Visual Studio installations with Visual Studio Setup Configuration utility DLL"""

import os
import ctypes

def get_vs_installations():

    class ISetupInstanceVTable(ctypes.Structure):
        """Class matching VisualStudio Setup package ISetupInstance vtable"""
        pass

    class ISetupInstance(ctypes.Structure):
        """COM interface for ISetupInstance"""
        _fields_ = [('vtable', ctypes.POINTER(ISetupInstanceVTable))]

    class IEnumSetupInstancesVTable(ctypes.Structure):
        """Class matching VisualStudio Setup package IEnumSetupInstances vtable"""
        pass

    class IEnumSetupInstances(ctypes.Structure):
        """COM interface for IEnumSetupInstances"""
        _fields_ = [('vtable', ctypes.POINTER(IEnumSetupInstancesVTable))]

    class ISetupConfigurationVTable(ctypes.Structure):
        """Class matching VisualStudio Setup package ISetupConfiguration vtable"""
        pass

    class ISetupConfiguration(ctypes.Structure):
        """COM interface for ISetupConfiguration"""
        _fields_ = [('vtable', ctypes.POINTER(ISetupConfigurationVTable))]

    proto_get_installation_path = ctypes.WINFUNCTYPE(
        ctypes.c_int,
        ctypes.POINTER(ISetupInstance),
        ctypes.POINTER(ctypes.c_wchar_p))

    proto_get_installation_version = ctypes.WINFUNCTYPE(
        ctypes.c_int,
        ctypes.POINTER(ISetupInstance),
        ctypes.POINTER(ctypes.c_wchar_p))

    ISetupInstanceVTable._fields_ = (
        ('QueryInterface', ctypes.c_void_p),
        ('AddRef', ctypes.c_void_p),
        ('Release', ctypes.c_void_p),
        ('GetInstanceId', ctypes.c_void_p),
        ('GetInstallDate', ctypes.c_void_p),
        ('GetInstallationName', ctypes.c_void_p),
        ('GetInstallationPath', proto_get_installation_path),
        ('GetInstallationVersion', proto_get_installation_version),
        ('GetDisplayName', ctypes.c_void_p),
        ('GetDescription', ctypes.c_void_p),
        ('ResolvePath', ctypes.c_void_p))

    proto_next = ctypes.WINFUNCTYPE(
        ctypes.c_int,
        ctypes.POINTER(IEnumSetupInstances),
        ctypes.c_int,
        ctypes.POINTER(ctypes.POINTER(ISetupInstance)),
        ctypes.POINTER(ctypes.c_int))

    IEnumSetupInstancesVTable._fields_ = (
        ('QueryInterface', ctypes.c_void_p),
        ('AddRef', ctypes.c_void_p),
        ('Release', ctypes.c_void_p),
        ('Next', proto_next),
        ('Skip', ctypes.c_void_p),
        ('Reset', ctypes.c_void_p),
        ('Clone', ctypes.c_void_p))

    proto_enum_instances = ctypes.WINFUNCTYPE(
        ctypes.c_int,
        ctypes.POINTER(ISetupConfiguration),
        ctypes.POINTER(ctypes.POINTER(IEnumSetupInstances)))

    ISetupConfigurationVTable._fields_ = (
        ('QueryInterface', ctypes.c_void_p),
        ('AddRef', ctypes.c_void_p),
        ('Release', ctypes.c_void_p),
        ('EnumInstances', proto_enum_instances),
        ('GetInstanceForCurrentProcess', ctypes.c_void_p),
        ('GetInstanceForPath', ctypes.c_void_p))

    proto_get_setup_configuration = ctypes.WINFUNCTYPE(
        ctypes.c_int,
        ctypes.POINTER(ctypes.POINTER(ISetupConfiguration)),
        ctypes.c_void_p)

    installations = []
    dll = None

    dll_path = os.path.expandvars("$ProgramData\\Microsoft\\VisualStudio\\Setup\\x64\\Microsoft.VisualStudio.Setup.Configuration.Native.dll")
    try:
        dll = ctypes.WinDLL(dll_path)
    except OSError as e:
        #print("Failed to load Visual Studio setup configuration DLL: " + str(e))
        return installations

    params_get_setup_configuration = (1, "configuration", 0), (1, "reserved", 0),

    get_setup_configuration = proto_get_setup_configuration(("GetSetupConfiguration", dll), params_get_setup_configuration)

    configuration = ctypes.POINTER(ISetupConfiguration)()
    reserved = ctypes.c_void_p(0)

    result = get_setup_configuration(ctypes.byref(configuration), reserved)
    if result != 0:
        #print("Failed to get setup configuration: " + str(result))
        return installations

    enum_instances = configuration.contents.vtable.contents.EnumInstances

    enum_setup_instances = ctypes.POINTER(IEnumSetupInstances)()
    result = enum_instances(configuration, ctypes.byref(enum_setup_instances))
    if result != 0:
        #print("Failed to enum setup instances: " + str(result))
        return installations


    setup_instance = ctypes.POINTER(ISetupInstance)()
    fetched = ctypes.c_int(0)

    while True:
        next = enum_setup_instances.contents.vtable.contents.Next
        result = next(enum_setup_instances, 1, ctypes.byref(setup_instance), ctypes.byref(fetched))
        if result == 1 or fetched == 0:
            break
        if result != 0:
            #print("Failed to get next setup instance: " + str(result))
            break

        version = ctypes.c_wchar_p()
        path = ctypes.c_wchar_p()
        
        get_installation_version = setup_instance.contents.vtable.contents.GetInstallationVersion
        get_installation_path = setup_instance.contents.vtable.contents.GetInstallationPath

        result = get_installation_version(setup_instance, ctypes.byref(version))
        if result != 0:
            #print("Failed to get setup instance version: " + str(result))
            break

        result = get_installation_path(setup_instance, ctypes.byref(path))
        if result != 0:
            #print("Failed to get setup instance version: " + str(result))
            break

        installations.append((version.value, path.value))

    return installations


if __name__ == "__main__":

    installations = get_vs_installations()

    for version, path in installations:
        print(version + " " + path)
