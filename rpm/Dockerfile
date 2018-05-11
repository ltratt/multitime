ARG OS=fedora:27
FROM ${OS}

SHELL ["bash", "-euxvc"]

RUN dnf install -y rpm-build dnf-utils rpmdevtools;\
    dnf clean all

# Bootstrap
RUN dnf install -y automake gcc;\
    dnf clean all

RUN mkdir -p /root/rpmbuild/SOURCES

CMD set -eu; \
    yum-builddep -y /src/rpm/SPECS/multitime.spec; \
    # if [ -e "/src/rpm/SOURCES/multitime.tar.gz" ]; then \
    #   ln -s /src/rpm/SOURCES/multitime.tar.gz /root/rpmbuild/SOURCES/; \
    # else \
    # # Preparing tarball
    #   tar --transform "s|^\./|/multitime/|" -cf /root/rpmbuild/SOURCES/multitime.tar.gz -C /src --exclude=.git --exclude SOURCES .; \
    # fi; \
    spectool -g -R /src/rpm/SPECS/multitime.spec; \
    rpmbuild -ba /src/rpm/SPECS/multitime.spec; \
    chown -R ${IDS-1000:1000} /root/rpmbuild/RPMS /root/rpmbuild/SRPMS; \
    cp -ra /root/rpmbuild/RPMS /root/rpmbuild/SRPMS /src/rpm/