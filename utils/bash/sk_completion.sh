_sk_completion()
{
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    local cmds="strike surge clean init purge cache config status build run"

    local opts=(
	            --verbose
	            --silent
				--version
				--help
				--force
				--profile 
				--memstat
				--dry
				--eval-dump
				--token-dump
				--node-dump
				--add-cc
				--gen-cmds
				--full
				--nuke
				--set
				-j
				-C
				)

    case "$prev" in
        surge)
            # could complete target names from manifest if it exists
            COMPREPLY=()
            return 0
            ;;
        -C)
            COMPREPLY=($(compgen -d -- "$cur"))
            return 0
            ;;
    esac

    if [[ "$cur" == -* ]]; then
        COMPREPLY=($(compgen -W "${opts[*]}" -- "$cur"))
    else
        COMPREPLY=($(compgen -W "$cmds" -- "$cur"))
    fi
}

complete -F _sk_completion sk
