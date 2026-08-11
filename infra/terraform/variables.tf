variable "aws_region" {
  description = "AWS region to deploy into"
  type        = string
  default     = "us-east-1"
}

variable "project" {
  description = "Name prefix applied to all resources"
  type        = string
  default     = "limit-order-book"
}

variable "instance_type" {
  description = "EC2 instance type for the app host"
  type        = string
  default     = "t3.small"
}

variable "db_instance_class" {
  description = "RDS instance class"
  type        = string
  default     = "db.t3.micro"
}

variable "db_name" {
  type    = string
  default = "orderbook"
}

variable "db_username" {
  type    = string
  default = "lob"
}

variable "db_password" {
  description = "RDS master password (sensitive). Set via TF_VAR_db_password or tfvars."
  type        = string
  sensitive   = true
}

variable "ssh_key_name" {
  description = "Name of an EXISTING EC2 key pair for SSH access"
  type        = string
}

variable "admin_cidr" {
  description = "CIDR allowed to reach SSH (22) and the app. Use your.ip/32."
  type        = string
}

variable "app_port" {
  description = "Backend port exposed on the instance"
  type        = number
  default     = 8000
}