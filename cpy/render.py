import inspect, importlib, functools, logging, typing, atexit, collections
from . import common, ConversionError

log = logging.getLogger(__name__)
# logging.basicConfig(level=logging.INFO)

################################################################################

class Config:
    def __init__(self, methods):
        self._set_debug = methods['set_debug']
        self._get_debug = methods['debug']
        self.set_type_error = methods['set_type_error']
        self.set_type_names = methods['set_type_names']
        self.set_type = methods['set_type']
        self.set_output_conversion = methods['set_output_conversion']
        self.set_input_conversion = methods['set_input_conversion']

    @property
    def debug(self):
        return self._get_debug().cast(bool)

    @debug.setter
    def debug(self, value):
        self._set_debug(bool(value))

################################################################################

def _render_module(pkg, doc, set_type_names):
    log.info('rendering document into module %s', repr(pkg))
    config, out = Config(doc), doc.copy()
    config.set_type_error(ConversionError)

    modules, translate = set(), {}

    # render classes and methods
    out['types'] = {k: v for k, v in doc['contents'] if isinstance(v, tuple)}
    for k, (meth, data) in out['types'].items():
        mod, old, cls = render_type(pkg, (doc['Variable'],), k, dict(meth), lookup=doc)
        if old is not None:
            translate[old] = cls
        modules.add(mod)
        cls.metadata = {k: v or None for k, v in data}
        for k, v in data:
            config.set_type(k, cls)

    # put type names if desired
    if set_type_names:
        names = tuple((o[0], k) for k, v in out['types'].items() for o in v[1])
        config.set_type_names(names)

    # render global objects (including free functions)
    out['objects'] = {k: render_object(pkg, k, v, out)
        for k, v in doc['contents'] if not isinstance(v, tuple)}

    out['scalars'] = common.find_scalars(doc['scalars'])

    # Monkey-patch modules based on redefined types (takes care of simple cases at least)
    mod = importlib.import_module(pkg)

    for mod in tuple(modules):
        parts = mod.__name__.split('.')
        for i in range(1, len(parts)):
            try:
                modules.add(importlib.import_module('.'.join(parts[:i])))
            except ImportError:
                pass

    for mod in modules:
        log.info('rendering monkey-patching module {}'.format(mod))
        for k in dir(mod):
            try:
                setattr(mod, k, translate[getattr(mod, k)])
                log.info('monkey-patching module {} attribute {}'.format(mod, k))
            except (TypeError, KeyError):
                pass

    log.info('finished rendering document into module %s', repr(pkg))
    return out, config

################################################################################

def render_module(pkg: str, doc: dict, set_type_names=False):
    try:
        return _render_module(pkg, doc, set_type_names)
    except BaseException:
        doc['clear_global_objects']()
        raise
    finally:
        atexit.register(common.finalize, doc['clear_global_objects'], log)

################################################################################

def render_init(init):
    if init is None:
        def __init__(self):
            raise TypeError('No __init__ is possible since no C++ constructors were declared')
    else:
        def __init__(self, *args, _new=init, return_type=None, signature=None):
            self.move_from(_new(*args, return_type=return_type, signature=signature))
    return __init__

################################################################################

def render_member(key, value, old, globalns={}, localns={}):
    if old is None:
        def fget(self, _old=value):
            return _old(self)._set_ward(self)
    else:
        def fget(self, _old=value, _return=old):
            out = _old(self)._set_ward(self)
            ret = common.eval_type(typing._type_check(_return, 'expected type'), globalns, localns)
            return out.cast(ret)

    def fset(self, other, _old=value):
        _old(self).copy_from(other)

    return property(fget=fget, fset=fset, doc='C++ member of type {}'.format(old))

################################################################################

def copy(self):
    other = self.__new__(type(self))
    other.copy_from(self)
    return other

################################################################################

def render_type(pkg: str, bases: tuple, name: str, methods, lookup={}):
    '''Define a new type in pkg'''
    mod, name = common.split_module(pkg, name)
    localns, globalns = mod.__dict__, lookup
    try:
        old_cls = getattr(mod, name)
        props = {}
    except AttributeError:
        old_cls = None
        props = {'__module__': mod.__name__}

    if old_cls is not None:
        for cls in old_cls.mro()[:-1]: # skip base class 'object'
            props.update(cls.__dict__)

    new = props.pop('__new__', None)
    if callable(new):
        log.warning('{}.{}.__new__ will not be rendered'.format(pkg, name))
    methods['__init__'] = render_init(methods.pop('new', None))

    for k, v in methods.items():
        k = common.translations.get(k, k)
        if k.startswith('.'):
            old = props.get('__annotations__', {}).get(k[1:])
            log.info("deriving member '%s.%s%s' from %s", mod.__name__, name, k, repr(old))
            props[k[1:]] = render_member(k[1:], v, old, globalns=globalns, localns=localns)
        else:
            old = props.get(k, common.default_methods.get(k))
            log.info("deriving method '%s.%s.%s' from %s", mod.__name__, name, k, repr(old))
            props[k] = render_function(v, old, globalns=globalns, localns=localns)

    props.setdefault('copy', copy)

    cls = type(name, bases, props)
    log.info("rendering class '%s.%s'", mod.__name__, name)
    setattr(mod, name, cls)
    return mod, old_cls, cls

################################################################################

def render_object(pkg, key, value, lookup={}):
    '''put in the module level functions and objects'''
    mod, key = common.split_module(pkg, key)
    log.info("rendering object '%s.%s'", mod.__name__, key)
    localns = mod.__dict__
    globalns = lookup

    old = getattr(mod, key, None)
    if old is None:
        pass
    elif callable(value):
        log.info("deriving function '%s.%s' from %s", mod.__name__, key, repr(old))
        assert callable(old), 'expected annotation to be a function'
        value = render_function(value, old, localns=localns, globalns=globalns)
    else:
        log.info("deriving object '%s.%s' from %s", mod.__name__, key, repr(old))
        if not isinstance(old, type):
            print(value.type())
            raise TypeError('expected placeholder {} to be a type'.format(old))
        value = value.cast(common.eval_type(old, localns=localns, globalns=globalns))

    setattr(mod, key, value)
    return value

################################################################################

def render_callback(_orig, _types):
    if not callable(_orig):
        return _orig
    def callback(*args):
        return _orig(*(a.cast(t) for a, t in zip(args, _types)))
    return callback

if not hasattr(typing, 'CallableMeta'):
    raise ImportError('Python 3.7 has a bug where typing.CallableMeta is missing')

################################################################################

def render_function(fun, old, globalns={}, localns={}):
    '''
    Replace a declared function's contents with the one from the document
    - Otherwise if the declared function takes 'function', call the declared function
    with the document function passed as 'function' and the other arguments filled appropiately
    - Otherwise, call the document function
    '''
    if old is None:
        def bound(*args, _orig=fun):
            return _orig(*args)
        return functools.update_wrapper(bound, common.opaque_signature)

    if isinstance(old, property):
        return property(render_function(fun, old.fget))
    ev = lambda t: common.eval_type(t, globalns, localns)

    sig = inspect.signature(old)

    has_fun = '_fun_' in sig.parameters
    if has_fun:
        sig = common.discard_parameter(sig, '_fun_')

    process = lambda t: tuple(map(ev, t.__args__[:-1])) if isinstance(t, typing.CallableMeta) else ev(t)
    types = [process(p.annotation) for p in sig.parameters.values()]
    empty = inspect.Parameter.empty

    if '_old' in sig.parameters:
        raise ValueError('Function {} was already wrapped'.format(old))
    if has_fun:
        def wrap(*args, _orig=fun, _bind=sig.bind, _old=old, gil=None, signature=None, **kwargs):
            bound = _bind(*args, **kwargs)
            bound.apply_defaults()
            args = (a if t is empty or a is None else render_callback(a, t) for a, t in zip(bound.arguments.values(), types))
            return _old(*args, _fun_=_orig, **bound.kwargs)
    else:
        ret = sig.return_annotation
        assert 'Variable' in globalns

        def wrap(*args, _orig=fun, _bind=sig.bind, _return=ret, gil=None, signature=None, **kwargs):
            bound = _bind(*args, **kwargs)
            bound.apply_defaults()
            out = _orig(*(render_callback(a, t) if isinstance(t, tuple) else a for a, t in zip(bound.arguments.values(), types)))
            if _return is empty:
                return out # no cast
            if _return is None or _return is type(None):
                return # return None regardless of output
            if out is None:
                out = globalns['Variable']() # unlikely, but replace None with empty Variable to try a cast
            return out.cast(ev(typing._type_check(_return, 'expected type')))

    return functools.update_wrapper(wrap, old)
