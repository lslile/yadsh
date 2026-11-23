# yadsh — yet another distributed shell

`dsh` (distributed shell) issues a command to a group of machines in parallel.
**yadsh** is a small, portable C reimplementation of the `dsh` from IBM's PSSP
package (as described in the PSSP 3.5 manual) for the SP2 platform.

It fans a command out to many hosts over a configurable remote shell (ssh/rsh)
and collects each host's output a line at a time, so results from different
hosts don't interleave. It can also act as a generic parallel queue for any
command, via the `DSH_REMOTE_CMD` / `DSH_PATH` environment variables and the
`-o` pass-through option.

## Build

yadsh uses autotools:

```sh
./autogen.sh      # if building from a git checkout
./configure
make
sudo make install
```

## Usage

```sh
# run a command on three named hosts
dsh -w host1,host2,host3 uptime

# take the host list from a working-collective file
WCOLL=./hosts dsh uname -a

# group identical output per host with dshbak
dsh -w host1,host2,host3 'rpm -q bash' | dshbak -c
```

See `man dsh` and `man dshbak` for the full option set.

Companion:

- **`dshbak`** — formats and groups `host: line` output per host; `-c` coalesces
  hosts with identical output.

## Status

Actively modernized. Known rough edges: the interactive command shell is still
minimal, and the man pages are a work in progress. Issues and pull requests are
welcome.

## License

BSD 2-Clause — see [`LICENSE`](LICENSE). Every source file carries the full
notice.
