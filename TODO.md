## Some bugs and incompletes 
- builtin set is broken
- builtin documentation is not finished

## Necessary improvements
- marge allocators from ``allocators.c`` to Vector implementation.
- add CTRL-C handling (maybe add option to disable it with microshell command line arguments)

## Improvements
- add ``print`` command and ``println`` commands
- support for variables with ``get-var`` and ``set-var`` commands (set-var should accept command line input like ``set-var name value`` or ``print value | set-var name``)
- option to ``push-history`` and ``pop-history`` to provide some scripting abilities
- when printing ``\w`` option of PS1 change ``$HOME`` to ``~``
- stderr-to-stdout stream utility (for using more with errors)

## Funny features
- add Commodore Basic syntax for editiing history ``[history entry number] command`` (must change history representation from ``Vector`` to ``OrderedMap`` to ``support history entry number with sope spacing``). Example script:
```basic
10 echo "Hello World"
goto 10
```

## Cleanup
- list functions at top of file with some documentation (maybe in markdown format?)
