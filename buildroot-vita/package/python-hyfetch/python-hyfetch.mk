################################################################################
#
# python-hyfetch
#
################################################################################

PYTHON_HYFETCH_VERSION = 1.99.0
PYTHON_HYFETCH_SOURCE = HyFetch-$(PYTHON_HYFETCH_VERSION).tar.gz
PYTHON_HYFETCH_SITE = https://files.pythonhosted.org/packages/1f/7d/7acc8fd22a1a4861f6a3833fbba8d1ffc6d118d143a4cbaab7f998867b4e
PYTHON_HYFETCH_SETUP_TYPE = setuptools
PYTHON_HYFETCH_LICENSE = MIT
PYTHON_HYFETCH_LICENSE_FILES = LICENSE.md
PYTHON_HYFETCH_DEPENDENCIES = python3 neofetch

$(eval $(python-package))
