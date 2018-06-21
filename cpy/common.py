def not_colored(s, *args, **kwargs):
    return s

try:
    from termcolor import colored
except ImportError:
    colored = not_colored

import sys

try:
    from contextlib import ExitStack
except ImportError:
    from contextlib2 import ExitStack

try:
    FileError = FileNotFoundError
except NameError:
    FileError = IOError

################################################################################

DELIMITER = '/'

EVENTS = ['Failure', 'Success', 'Exception', 'Timing']

COLORED_EVENTS = [
    colored('Failure',   'red'),
    colored('Success',   'green'),
    colored('Exception', 'red'),
    colored('Timing',    'yellow')
]

################################################################################

def events(color=False):
    return COLORED_EVENTS if color else EVENTS

################################################################################

class Report:
    '''Basic interface for a Report object'''
    def __enter__(self):
        return self

    def finalize(self, *args):
        pass

    def __exit__(self, value, cls, traceback):
        pass

################################################################################

def pop_value(key, keys, values, default=None):
    try:
        idx = keys.index(key)
        keys.pop(idx)
        return values.pop(idx)
    except ValueError:
        return default

################################################################################

def import_library(lib):
    import os, importlib
    sys.path.insert(0, os.path.dirname(os.path.abspath(lib)))
    return importlib.import_module(lib)

################################################################################

def open_file(stack, name, mode):
    '''Open file with ExitStack()'''
    if name in ('stderr', 'stdout'):
        return getattr(sys, name)
    else:
        return stack.enter_context(open(name, mode))

################################################################################

def load_parameters(params):
    if not params:
        return {}
    elif not isinstance(params, str):
        return dict(params)
    try:
        with open(params) as f:
            import json
            return dict(json.load(f))
    except FileError:
        return eval(params)

################################################################################

def test_indices(lib, exclude=False, tests=None, regex=''):
    '''
    Return list of indices of tests to run
        exclude: whether to include or exclude the specified tests
        tests: list of manually specified tests
        regex: pattern to specify tests
    '''
    if tests:
        indices = set(lib.find_test(t) for t in tests)
    elif not regex:
        indices = set() if exclude else set(range(lib.n_tests()))

    if regex:
        import re
        pattern = re.compile(regex)
        indices.update(i for i, t in enumerate(lib.test_names()) if pattern.match(t))

    if exclude:
        indices = set(range(lib.n_tests())).difference(indices)
    return sorted(indices)

################################################################################

def parametrized_indices(lib, indices, params={}):
    '''
    Yield tuple of (index, parameter_pack) for each test to run
        lib: the cpy library object
        indices: the possible indices to yield from
        params: dict or list of specified parameters (e.g. from load_parameters())
    '''
    names = lib.test_names()
    for i in indices:
        try: # params is dict-like
            ps = list(params.get(names[i], [None]))
        except AttributeError: # params gives a constant value for all keys
            ps = params
        n = lib.n_parameters(i)
        while None in ps:
            ps.remove(None)
            ps.extend(range(n))
        for p in ps:
            if isinstance(p, int) and p >= n:
                raise IndexError("Parameter pack index {} is out of range for test '{}' (n={})".format(j, names[i], n))
            yield i, p

################################################################################

class MultiReport:
    '''Simple wrapper to call multiple reports from C++ as if they are one'''
    def __init__(self, reports):
        self.reports = reports

    def __call__(self, index, scopes, logs):
        for r in self.reports:
            r(index, scopes, logs)

def multireport(reports):
    '''Wrap multiple reports for C++ to look like they are one'''
    if not reports:
        return None
    if len(reports) == 1:
        return reports[0]
    return MultiReport(reports)

################################################################################

def run_test(lib, index, test_masks, args=(), gil=False, cout=False, cerr=False):
    lists = [[] for _ in events()]

    with ExitStack() as stack:
        for r, mask in test_masks:
            stack.enter_context(r)
            [l.append(r) for m, l in zip(mask, lists) if m]
        reports = tuple(map(multireport, lists))
        return lib.run_test(index, reports, args, gil, cout, cerr)

################################################################################

def readable_message(kind, scopes, logs, indent='    '):
    '''Return readable string for a C++ cpy callback'''
    keys, values = map(list, zip(*logs))
    line, path = (pop_value(k, keys, values) for k in ('line', 'file'))
    scopes = repr(DELIMITER.join(scopes))

    # first line
    if path is None:
        s = '{}: {}\n'.format(kind, scopes)
    else:
        desc = '({})'.format(path) if line is None else '({}:{})'.format(path, line)
        s = '{}: {} {}\n'.format(kind, scopes, desc)

    # comments
    while 'comment' in keys:
        s += indent + 'comment: ' + repr(pop_value('comment', keys, values)) + '\n'

    # comparisons
    comp = ('lhs', 'op', 'rhs')
    while all(c in keys for c in comp):
        lhs, op, rhs = (pop_value(k, keys, values) for k in comp)
        s += indent + 'required: {} {} {}\n'.format(lhs, op, rhs)

    # all other logged keys and values
    for k, v in zip(keys, values):
        s += indent + (k + ': ' if k else '') + repr(v) + '\n'

    return s
