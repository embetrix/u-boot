
EMACS_PREFIX = r'(?:[0-9]{4}.*\.patch:[0-9]+: )?'
TYPE_NAME = r'([A-Z_]+:)?'
RE_ERROR = re.compile(r'ERROR:%s (.*)' % TYPE_NAME)
RE_WARNING = re.compile(EMACS_PREFIX + r'WARNING:%s (.*)' % TYPE_NAME)
RE_CHECK = re.compile(r'CHECK:%s (.*)' % TYPE_NAME)
RE_FILE = re.compile(r'#(\d+): (FILE: ([^:]*):(\d+):)?')
RE_NOTE = re.compile(r'NOTE: (.*)')


def CheckPatchParseOneMessage(message):
    """Parse one checkpatch message
        message: string to parse

    Returns:
        dict:
            'type'; error or warning
            'msg': text message
            'file' : filename
            'line': line number
    """

    if RE_NOTE.match(message):
        return {}

    item = {}

    err_match = RE_ERROR.match(message)
    warn_match = RE_WARNING.match(message)
    check_match = RE_CHECK.match(message)
    if err_match:
        item['cptype'] = err_match.group(1)
        item['msg'] = err_match.group(2)
        item['type'] = 'error'
    elif warn_match:
        item['cptype'] = warn_match.group(1)
        item['msg'] = warn_match.group(2)
        item['type'] = 'warning'
    elif check_match:
        item['cptype'] = check_match.group(1)
        item['msg'] = check_match.group(2)
        item['type'] = 'check'
    else:
        message_indent = '    '
        print('patman: failed to parse checkpatch message:\n%s' %
              (message_indent + message.replace('\n', '\n' + message_indent)),
              file=sys.stderr)
        return {}

    file_match = RE_FILE.search(message)
    # some messages have no file, catch those here
    no_file_match = any(s in message for s in [
        '\nSubject:', 'Missing Signed-off-by: line(s)',
        'does MAINTAINERS need updating'
    ])

    if file_match:
        err_fname = file_match.group(3)
        if err_fname:
            item['file'] = err_fname
            item['line'] = int(file_match.group(4))
        else:
            item['file'] = '<patch>'
            item['line'] = int(file_match.group(1))
    elif no_file_match:
        item['file'] = '<patch>'
    else:
        message_indent = '    '
        print('patman: failed to find file / line information:\n%s' %
              (message_indent + message.replace('\n', '\n' + message_indent)),
              file=sys.stderr)

    return item


def CheckPatchParse(checkpatch_output, verbose=False):
    """Parse checkpatch.pl output

    Args:
        checkpatch_output: string to parse
            stdout: checkpatch_output
    result.stdout = checkpatch_output
    emacs_stats = r'(?:[0-9]{4}.*\.patch )?'
                          r'total: (\d+) errors, (\d+) warnings, (\d+)')
                               r'total: (\d+) errors, (\d+) warnings, (\d+)'
                               r' checks, (\d+)')
    re_ok = re.compile(r'.*has no obvious style problems')
    re_bad = re.compile(r'.*has style problems, please review')

    # A blank line indicates the end of a message
    for message in result.stdout.split('\n\n'):
            print(message)

        # either find stats, the verdict, or delegate
        match = re_stats_full.match(message)
            match = re_stats.match(message)
        elif re_ok.match(message):
        elif re_bad.match(message):
            problem = CheckPatchParseOneMessage(message)
            if problem:
                result.problems.append(problem)

def CheckPatch(fname, verbose=False, show_types=False):
    """Run checkpatch.pl on a file and parse the results.

    Args:
        fname: Filename to check
        verbose: True to print out every line of the checkpatch output as it is
            parsed
        show_types: Tell checkpatch to show the type (number) of each message

    Returns:
        namedtuple containing:
            ok: False=failure, True=ok
            problems: List of problems, each a dict:
                'type'; error or warning
                'msg': text message
                'file' : filename
                'line': line number
            errors: Number of errors
            warnings: Number of warnings
            checks: Number of checks
            lines: Number of lines
            stdout: Full output of checkpatch
    """
    chk = FindCheckPatch()
    args = [chk, '--no-tree']
    if show_types:
        args.append('--show-types')
    output = command.Output(*args, fname, raise_on_error=False)

    return CheckPatchParse(output, verbose)

