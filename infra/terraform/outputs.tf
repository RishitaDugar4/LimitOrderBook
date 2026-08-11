output "app_public_ip" {
  description = "Public IP of the EC2 app host"
  value       = aws_instance.app.public_ip
}

output "app_public_dns" {
  description = "Public DNS of the EC2 app host"
  value       = aws_instance.app.public_dns
}

output "rds_endpoint" {
  description = "RDS Postgres endpoint (host:port)"
  value       = aws_db_instance.postgres.endpoint
}

output "database_url" {
  description = "DATABASE_URL for the backend (password omitted)"
  value       = "postgresql+psycopg://${var.db_username}:<password>@${aws_db_instance.postgres.endpoint}/${var.db_name}"
}

output "frontend_url" {
  value = "http://${aws_instance.app.public_dns}"
}