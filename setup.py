from setuptools import setup

setup(
    name="wcpp",
    version="1.0.0",
    install_requires=['crc', 'pyserial', 'rich', 'getchlib'],
    packages=['wcpp'],
    package_dir={'wcpp': 'python'},
    entry_points={
        'console_scripts':[
            'wcpp-util = wcpp.util:main',
        ],
    },
)
