import lldb
import operator
import re

commands_prefix = 'rejit_'
commands = []


class Command:
  def __init__(self, name, func, help):
    self.name = commands_prefix + name
    self.func = func
    self.help = help



# ----- Help -------------------------------------------------------------------
def fn_help(debugger, command, result, dict):
  print '''\
lldb helpers to debug rejit.
Availble commands:'''
  for c in commands:
    print '\t%s:' % c.name
    help = c.help
    help = re.sub('^', '\t\t', help)
    help = re.sub('\n', '\n\t\t', help)
    print help
help_help = 'Prints this message.'
help = Command('help', fn_help, help_help)
commands += [help]



# ----- State ------------------------------------------------------------------
kPointerSize = 8

def fn_state(debugger, command, result, dict):
  target = debugger.GetSelectedTarget()
  opts = lldb.SBExpressionOptions()
  def eval_expr(type, c_expr):
    return type(target.EvaluateExpression(c_expr, opts).GetValue())
  CalleeSavedRegsSize = eval_expr(int, 'CalleeSavedRegsSize()')
  kStateInfoSize = 6 * kPointerSize

  state_fp_relative_fields = [
    ('time_summary_last', -CalleeSavedRegsSize - kStateInfoSize),
    ('result_matches',    -CalleeSavedRegsSize - 1 * kPointerSize),
    ('ff_position',       -CalleeSavedRegsSize - 2 * kPointerSize),
    ('ff_found_state',    -CalleeSavedRegsSize - 3 * kPointerSize),
    ('backward_match',    -CalleeSavedRegsSize - 4 * kPointerSize),
    ('forward_match',     -CalleeSavedRegsSize - 5 * kPointerSize),
    ('last_match_end',    -CalleeSavedRegsSize - 6 * kPointerSize),
  ]
  print 'raw stack:'
  debugger.HandleCommand('register read fp')
  debugger.HandleCommand('register read sp')
  debugger.HandleCommand('memory read $sp -c `($fp - $sp) / sizeof(void*) + 1` -f pointer')
  print ''
  print 'string start:\t',
  debugger.HandleCommand('register read r13')
  print 'string pointer:\t',
  debugger.HandleCommand('register read r14')
  print 'string end:\t',
  debugger.HandleCommand('register read r15')
  print ''
  print 'fields:'
  def read_pointer_at_fp_offset(fp_relative_field):
    print fp_relative_field[0] + '\t',
    debugger.HandleCommand('memory read `$fp + %d` -c 1 -f pointer' % fp_relative_field[1])
  for field in state_fp_relative_fields:
    read_pointer_at_fp_offset(field)
  print 'state ring:'
  debugger.HandleCommand('memory read $sp -c `($fp - $sp - %d) / sizeof(void*)` -f pointer' % (kStateInfoSize + CalleeSavedRegsSize))

help_state = '''\
Use from a rejit frame. Parses the stack and displays detailed information about
the current state.'''
state = Command('state', fn_state, help_state)
commands += [state]






commands = sorted(commands, key=operator.attrgetter('name'))

def install_command(c, debugger, dict):
  debugger.HandleCommand(
    'command script add -f lldb_rejit.%s %s' % (c.func.__name__, c.name))


def __lldb_init_module (debugger, dict):
  for c in commands:
    install_command(c, debugger, dict)
  print 'rejit debugging helpers have been installed. Use `%s` for help.' % help.name
