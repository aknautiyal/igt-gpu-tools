# SPDX-License-Identifier: MIT
# Copyright © 2024 Intel Corporation

import json
import logging
import re
import typing

from dataclasses import dataclass
from pathlib import Path

import pytest

from bench import exceptions
from bench.helpers.helpers import (modprobe_driver, modprobe_driver_check)
from bench.helpers.log import HOST_DMESG_FILE
from bench.configurators.vgpu_profile_config import VgpuProfileConfigurator, VfSchedulingMode
from bench.configurators.vgpu_profile import VgpuProfile
from bench.configurators.vmtb_config import VmtbConfigurator
from bench.machines.host import Host, Device
from bench.machines.virtual.vm import VirtualMachine


logger = logging.getLogger('Conftest')


def pytest_addoption(parser):
    parser.addoption('--vm-image',
                     action='store',
                     help='OS image to boot on VM')
    parser.addoption('--card',
                     action='store',
                     help='Device card index for test execution')


@dataclass
class VmmTestingConfig:
    """Structure represents test configuration used by a setup fixture.

    Available settings:
    - num_vfs: requested number of VFs to enable
    - max_num_vms: maximal number of VMs (the value can be different than enabled number of VFs)
    - scheduling_mode: requested vGPU scheduling profile (infinite maps to default 0's)
    - auto_poweron_vm: assign VFs and power on VMs automatically in setup fixture
    - auto_probe_vm_driver: probe guest DRM driver in setup fixture (VM must be powered on)
    - unload_host_drivers_on_teardown: unload host DRM drivers in teardown fixture
    - wa_reduce_vf_lmem: workaround to reduce VF LMEM (for save-restore/migration tests speed-up)
    """
    num_vfs: int = 1
    max_num_vms: int = 2
    scheduling_mode: VfSchedulingMode = VfSchedulingMode.INFINITE

    auto_poweron_vm: bool = True
    auto_probe_vm_driver: bool = True
    unload_host_drivers_on_teardown: bool = False
    # Temporary W/A: reduce size of LMEM assigned to VFs to speed up a VF state save-restore process
    wa_reduce_vf_lmem: bool = False

    def __str__(self) -> str:
        return f'{self.num_vfs}VF'

    def __repr__(self) -> str:
        return (f'\nVmmTestingConfig:'
                f'\nNum VFs = {self.num_vfs} / max num VMs = {self.max_num_vms}'
                f'\nVF scheduling mode = {self.scheduling_mode}'
                f'\nSetup flags:'
                f'\n\tVM - auto power-on = {self.auto_poweron_vm}'
                f'\n\tVM - auto DRM driver probe = {self.auto_probe_vm_driver}'
                f'\n\tHost - unload drivers on teardown = {self.unload_host_drivers_on_teardown}'
                f'\n\tW/A - reduce VF LMEM (improves migration time) = {self.wa_reduce_vf_lmem}')


class VmmTestingSetup:
    def __init__(self, vmtb_config: VmtbConfigurator, cmdline_config, host, testing_config):
        self.testing_config: VmmTestingConfig = testing_config
        self.host: Host = host

        self.dut_index = vmtb_config.get_host_config().card_index if cmdline_config['card_index'] is None \
                         else int(cmdline_config['card_index'])
        self.guest_os_image = vmtb_config.get_guest_config().os_image_path if cmdline_config['vm_image'] is None \
                         else cmdline_config['vm_image']

        self.vgpu_profiles_dir = vmtb_config.vmtb_config_file.parent / vmtb_config.config.vgpu_profiles_path

        self.host.dut_index = self.dut_index
        self.host.drm_driver_name = vmtb_config.get_host_config().driver
        self.host.igt_config = vmtb_config.get_host_config().igt_config

        self.host.load_drivers()
        self.host.discover_devices()

        logger.info("\nDUT info:"
                    "\n\tCard index: %s"
                    "\n\tPCI BDF: %s "
                    "\n\tDevice ID: %s (%s)"
                    "\n\tHost DRM driver: %s",
                    self.host.dut_index,
                    self.get_dut().pci_info.bdf,
                    self.get_dut().pci_info.devid, self.get_dut().gpu_model,
                    self.get_dut().driver.get_name())

        self.vgpu_profile: VgpuProfile = self.get_vgpu_profile()

        # Start maximum requested number of VMs, but not more than VFs supported by the given vGPU profile
        self.vms: typing.List[VirtualMachine] = [
            VirtualMachine(vm_idx, self.guest_os_image,
                           vmtb_config.get_guest_config().driver,
                           vmtb_config.get_guest_config().igt_config)
            for vm_idx in range(min(self.vgpu_profile.num_vfs, self.testing_config.max_num_vms))]

    def get_vgpu_profile(self) -> VgpuProfile:
        configurator = VgpuProfileConfigurator(self.vgpu_profiles_dir, self.get_dut().gpu_model)
        try:
            vgpu_profile = configurator.get_vgpu_profile(self.testing_config.num_vfs,
                                                         self.testing_config.scheduling_mode)
        except exceptions.VgpuProfileError as exc:
            logger.error("Suitable vGPU profile not found: %s", exc)
            raise exceptions.VgpuProfileError('Invalid test setup - vGPU profile not found!')

        vgpu_profile.print_parameters()

        return vgpu_profile

    def get_dut(self) -> Device:
        try:
            return self.host.gpu_devices[self.dut_index]
        except IndexError as exc:
            logger.error("Invalid VMTB config - device card index = %s not available", self.dut_index)
            raise exceptions.VmtbConfigError(f'Device card index = {self.dut_index} not available') from exc

    @property
    def get_vm(self):
        return self.vms

    def get_num_vms(self) -> int:
        return len(self.vms)

    def poweron_vms(self):
        for vm in self.vms:
            vm.poweron()

    def poweroff_vms(self):
        for vm in self.vms:
            if vm.is_running():
                try:
                    vm.poweroff()
                except Exception as exc:
                    self.testing_config.unload_host_drivers_on_teardown = True
                    logger.warning("Error on VM%s poweroff (%s)", vm.vmnum, exc)

        if self.testing_config.unload_host_drivers_on_teardown:
            raise exceptions.GuestError('VM poweroff issue - cleanup on test teardown')

    def teardown(self):
        try:
            self.poweroff_vms()
        except Exception as exc:
            logger.error("Error on test teardown (%s)", exc)
        finally:
            num_vfs = self.get_dut().get_current_vfs()
            self.get_dut().remove_vfs()
            self.get_dut().reset_provisioning(num_vfs)
            self.get_dut().cancel_work()

            if self.testing_config.unload_host_drivers_on_teardown:
                self.host.unload_drivers()


@pytest.fixture(scope='session', name='get_vmtb_config')
def fixture_get_vmtb_config(create_host_log, pytestconfig):
    VMTB_CONFIG_FILE = 'vmtb_config.json'
    # Pytest Config.rootpath points to the VMTB base directory
    vmtb_config_file_path: Path = pytestconfig.rootpath / VMTB_CONFIG_FILE
    return VmtbConfigurator(vmtb_config_file_path)


@pytest.fixture(scope='session', name='create_host_log')
def fixture_create_host_log():
    if HOST_DMESG_FILE.exists():
        HOST_DMESG_FILE.unlink()
    HOST_DMESG_FILE.touch()


@pytest.fixture(scope='session', name='get_cmdline_config')
def fixture_get_cmdline_config(request):
    cmdline_params = {}
    cmdline_params['vm_image'] = request.config.getoption('--vm-image')
    cmdline_params['card_index'] = request.config.getoption('--card')
    return cmdline_params


@pytest.fixture(scope='session', name='get_host')
def fixture_get_host():
    return Host()


@pytest.fixture(scope='class', name='setup_vms')
def fixture_setup_vms(get_vmtb_config, get_cmdline_config, get_host, request):
    """Arrange VM environment for the VMM Flows test execution.

    VM setup steps follow the configuration provided as VmmTestingConfig parameter, including:
    host drivers probe (DRM and VFIO), provision and enable VFs, boot VMs and load guest DRM driver.
    Tear-down phase covers test environment cleanup:
    shutdown VMs, reset provisioning, disable VMs and optional host drivers unload.

    The fixture is designed for test parametrization, as the input to the following test class decorator:
    @pytest.mark.parametrize('setup_vms', set_test_config(max_vms=N), ids=idfn_test_config, indirect=['setup_vms'])
    where 'set_test_config' provides request parameter with a VmmTestingConfig (usually list of configs).
    """
    tc: VmmTestingConfig = request.param
    logger.debug(repr(tc))

    host: Host = get_host
    ts: VmmTestingSetup = VmmTestingSetup(get_vmtb_config, get_cmdline_config, host, tc)

    device: Device = ts.get_dut()
    num_vfs = ts.vgpu_profile.num_vfs
    num_vms = ts.get_num_vms()

    logger.info('[Test setup: %sVF-%sVM]', num_vfs, num_vms)

    # XXX: VF migration on discrete devices (with LMEM) is currently quite slow.
    # As a temporary workaround, reduce size of LMEM assigned to VFs to speed up a state save/load process.
    if tc.wa_reduce_vf_lmem and device.has_lmem():
        logger.debug("W/A: reduce VFs LMEM quota to accelerate state save/restore")
        org_vgpu_profile_vfLmem = ts.vgpu_profile.resources.vfLmem
        # Assign max 512 MB to VF
        ts.vgpu_profile.resources.vfLmem = min(ts.vgpu_profile.resources.vfLmem // 2, 536870912)

    device.provision(ts.vgpu_profile)

    assert device.create_vf(num_vfs) == num_vfs

    if tc.auto_poweron_vm:
        bdf_list = [device.get_vf_bdf(vf) for vf in range(1, num_vms + 1)]
        for vm, bdf in zip(ts.get_vm, bdf_list):
            vm.assign_vf(bdf)

        ts.poweron_vms()

        if tc.auto_probe_vm_driver:
            modprobe_cmds = [modprobe_driver(vm) for vm in ts.get_vm]
            for i, cmd in enumerate(modprobe_cmds):
                assert modprobe_driver_check(ts.get_vm[i], cmd), f'modprobe failed on VM{i}'

    logger.info('[Test execution: %sVF-%sVM]', num_vfs, num_vms)
    yield ts

    logger.info('[Test teardown: %sVF-%sVM]', num_vfs, num_vms)
    # XXX: cleanup counterpart for VFs LMEM quota workaround - restore original value
    if tc.wa_reduce_vf_lmem and device.has_lmem():
        ts.vgpu_profile.resources.vfLmem = org_vgpu_profile_vfLmem

    ts.teardown()


def idfn_test_config(test_config: VmmTestingConfig):
    """Provide test config ID in parametrized tests (e.g. test_something[V4].
    Usage: @pytest.mark.parametrize([...], ids=idfn_test_config, [...])
    """
    return str(test_config)


RESULTS_FILE = Path() / "results.json"
results = {
    "results_version": 10,
    "name": "results",
    "tests": {},
}


@pytest.hookimpl(hookwrapper=True)
def pytest_report_teststatus(report):
    yield
    with open(HOST_DMESG_FILE, 'r+', encoding='utf-8') as dmesg_file:
        dmesg = dmesg_file.read()
        test_string = re.findall('[A-Za-z_.]*::.*', report.nodeid)[0]
        results["name"] = f"vmtb_{test_string}"
        test_name = f"vmtb@{test_string}"
        if report.when == 'call':
            out = report.capstdout
            if report.passed:
                result = "pass"
                out = f"{test_name} passed"
            elif report.failed:
                result = "fail"
            else:
                result = "skip"
            result = {"out": out, "result": result, "time": {"start": 0, "end": report.duration},
                    "err": report.longreprtext, "dmesg": dmesg}
            results["tests"][test_name] = result
            dmesg_file.truncate(0)
        elif report.when == 'setup' and report.failed:
            result = {"out": report.capstdout, "result": "crash", "time": {"start": 0, "end": report.duration},
                    "err": report.longreprtext, "dmesg": dmesg}
            results["tests"][test_name] = result
            dmesg_file.truncate(0)


@pytest.hookimpl()
def pytest_sessionfinish():
    if RESULTS_FILE.exists():
        RESULTS_FILE.unlink()
    RESULTS_FILE.touch()
    jsonString = json.dumps(results, indent=2)
    with open(str(RESULTS_FILE), 'w',  encoding='utf-8') as f:
        f.write(jsonString)
