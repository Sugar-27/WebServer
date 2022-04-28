CREATE TABLE IF NOT EXISTS `user_info`(
   `id` INT(11) UNSIGNED AUTO_INCREMENT,
   `name` VARCHAR(50) DEFAULT NULL,
   `password` VARCHAR(200) NOT NULL,
   `age` INT(11) DEFAULT NULL,
   `sex` ENUM('male', 'female', 'privary'),
   PRIMARY KEY (`id`)
) ENGINE = InnoDB DEFAULT CHARSET = utf8;
INSERT INTO user_infor (name, password, age, sex) VALUES ('Tom', 'test_password', 10, 'male');
INSERT INTO user_info (name, password, age, sex) VALUES ('Amy', 'test_password2', 11, 'female');