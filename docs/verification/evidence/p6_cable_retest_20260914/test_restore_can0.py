"""Exercise restoration boundaries with mocked process and interface operations."""
import copy
import importlib.util
import tempfile
from pathlib import Path
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('restore', Path(__file__).with_name('restore_can0.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
state = [{'flags': ['UP'], 'mtu': 16, 'linkinfo': {'info_data': {
    'state': 'ERROR-ACTIVE', 'bittiming': {'bitrate': 500000},
    'restart_ms': 100, 'berr_counter': {'tx': 0, 'rx': 0}}}}]

for scenario in ('success', 'not_off', 'post_error', 'sender_active'):
    with tempfile.TemporaryDirectory() as temporary:
        module.BASE = Path(temporary)
        machine = Mock()
        machine.read_text.return_value = '6923ab3301fb4a8d816759b04ec6bf0a'
        proc = Mock()
        sender = Mock()
        sender.name = '123'
        sender.__truediv__ = Mock(return_value=Mock())
        sender.__truediv__.return_value.resolve.return_value.name = 'cangen'
        proc.iterdir.return_value = [sender] if scenario == 'sender_active' else []
        post = copy.deepcopy(state)
        if scenario == 'post_error':
            post[0]['linkinfo']['info_data']['state'] = 'ERROR-PASSIVE'
        with (
            patch.object(module, 'Path', side_effect=lambda p: machine if p == '/etc/machine-id' else proc),
            patch.object(module.os, 'geteuid', return_value=0),
            patch('builtins.input', return_value='NO' if scenario == 'not_off' else 'POWER_OFF'),
            patch.object(module, 'snapshot', side_effect=[state, post]),
            patch.object(module.subprocess, 'run') as run,
        ):
            failed = False
            try:
                module.main()
            except AssertionError:
                failed = True
            assert failed == (scenario != 'success'), scenario
            actions = [call.args[0][-1] for call in run.call_args_list]
            assert actions == ({'success': ['down', 'up'], 'post_error': ['down', 'up', 'down']}
                               .get(scenario, [])), (scenario, actions)
        print('PASS:', scenario)
