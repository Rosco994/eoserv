## EOServ Fork
This repository is a modified version of the EOServ project, enhanced with the following features:

- SLN
- Weddings
- Inn Sleeping

## Installation Guide for Linux
Follow these steps to install EOServ on Linux/Ubuntu, including configurations for MariaDB.

### Prerequisites
Install Dependencies:
- sudo apt install -y git cmake build-essential mariadb-server libmariadb-dev

### MariaDB Setup
Run Secure Installation:
- sudo /usr/bin/mysql_secure_installation

Follow the prompts:
- Enter current password for root (enter for none):
- Set root password? [Y/n]: y
- New password: Enter desired password, e.g. SUA!Fzjk9T@#
- Re-enter new password: (Re-enter the same password)
- Remove anonymous users? [Y/n]: y
- Disallow root login remotely? [Y/n]: Depends if you want to be able to connect remotely.
- Remove test database and access to it? [Y/n]: y
- Reload privilege tables now? [Y/n]: y

Create Database for EOServ:
- mariadb -u root -p
- CREATE DATABASE eoserv;

### Clone and Build EOServ
Clone the Repository:
- cd ~
- git clone https://github.com/Rosco994/eoserv.git

Navigate to the Directory and Build:
- cd eoserv
- cmake -DEOSERV_WANT_MYSQL=ON .
- make

## Credits

This project is built on contributions from the following people:

- **Vult-r**: Creator/owner of the original server and client engine code.
- **Julian Smythe**: Contributor to additional code improvements.
- **Niox**: Contributor to additional code improvements.
- **Cirras**: Contributor to additional code improvements.
- **Ethan Moffat**: Contributor to additional code improvements.

## License

This project is licensed under the MIT License. See the LICENSE file for details.
