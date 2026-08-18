# Agents

xspawn is a compiled C client. The **build host** has
the toolchain. The **runtime host** is the **target** and has no
Xcode, compiler, Homebrew, or cppcheck unless they happen to be
installed for other reasons.

`make`, `make test`, `make test-unit`, `make test-functional`,
`cc`, and **cppcheck** are build-host only. Do not treat
clone-and-compile as a deploy path onto the target.

Colocated build-and-test on one Mac is allowed. It is not a
clean-runtime proof.

When the CrowdStrike EDR product appears in docs, comments,
or CLI help, write **CrowdStrike Falcon**. Do not shorten it.

See README.md for the operator surface and ARCHITECTURE.md for the
wire and host model.
