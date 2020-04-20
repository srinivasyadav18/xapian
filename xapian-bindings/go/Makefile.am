GOTREE="/c/Users/appveyor/go"
GOSRCDIR = ${GOTREE}/src/xapian
TESTS = ./test
AM_TESTS_ENVIRONMENT = \
	abs_builddir='$(abs_builddir)' ;\
	srcdir='$(srcdir)'
	export abs_builddir ;\
	export srcdir ;
LOG_COMPILER = ${GO} test ./test

install-data-hook:
	cd ${GOSRCDIR} && ${GO} install -i -x -a

all : modify
	cd ${GOSRCDIR}
	${GO} build -x -work

# 	${GO} enw -w GOPATH=/mingw64
modify : build
	${MKDIR_P} ${GOTREE}
	${MKDIR_P} ${GOTREE}/bin
	${MKDIR_P} ${GOTREE}/src
	${MKDIR_P} ${GOTREE}/pkg
	${MKDIR_P} ${GOSRCDIR}
	cp -r * ${GOSRCDIR}

build :
	${SWIG} ${SWIG_FLAGS} -c++ -go -cgo -intgosize 64 go.i
	cd work && ${GO} run get_paths.go $(abs_top_builddir) ${USE_CLANG_GO_LIB}
	head -100 xapian.go
	${AWK} 'NR==18{print "#cgo CXXFLAGS: -I $(abs_top_builddir)../xapian-core/include/" RS\
		"#cgo CXXFLAGS: ${CXXFLAGS}" RS\
		"#cgo CPPFLAGS: ${CPPFLAGS}"}1' xapian.go > xapian2.go
	head -100 xapian.go
	mv xapian2.go xapian.go
	${GO} env -w CGO_CXXFLAGS="-I $(abs_top_builddir)/../xapian-core/include/"
	${GO} env -w CXX="${CXX}"
check :
	cd ${GOSRCDIR}/test && ${GO} test
clean :
	rm -r xapian.go
.PHONY: build modify run check
