# Deployment

Two ways to run the full stack.

## Local (Docker Compose)

Everything — Postgres, backend (C++ engine + FastAPI), and the React frontend —
in three containers:

```bash
cp .env.example .env          # edit POSTGRES_PASSWORD
docker compose up --build
```

- Frontend: http://localhost:8080
- Backend API/docs: http://localhost:8000/docs
- Postgres: localhost:5432

## AWS (EC2 + RDS)

I can write and validate all of this, but **you** run the `apply` — it creates
billed resources under your account and needs your AWS credentials.

### 1. Provision infrastructure

```bash
cd infra/terraform
cp terraform.tfvars.example terraform.tfvars   # fill in ssh_key_name, admin_cidr
export TF_VAR_db_password='a-strong-password'

terraform init
terraform plan
terraform apply
```

Terraform creates: a VPC with two public subnets, an EC2 app host (Docker
pre-installed via user_data), an RDS Postgres 16 instance reachable **only**
from the app host's security group, and the security groups locking SSH/HTTP to
your `admin_cidr`.

Note the outputs:

```bash
terraform output app_public_dns   # where the app will be served
terraform output rds_endpoint     # host:port for DATABASE_URL
```

### 2. Deploy the app to the host

RDS is already running; the EC2 host just needs the code + a `.env`:

```bash
# From the repo root, copy the source up (or `git clone` on the host):
rsync -av --exclude .git --exclude node_modules --exclude build \
      ./ ubuntu@$(terraform -chdir=infra/terraform output -raw app_public_dns):/opt/app/

ssh ubuntu@<app_public_dns>
cd /opt/app

# DATABASE_URL uses the RDS endpoint + the password you set above:
echo "DATABASE_URL=postgresql+psycopg://lob:<password>@<rds_endpoint>/orderbook" > .env

docker compose -f infra/deploy/docker-compose.aws.yml --env-file .env up --build -d
```

The RDS endpoint from `terraform output` already includes `:5432`.

### 3. Verify

```bash
curl http://<app_public_dns>/api/health        # {"status":"ok",...}
open http://<app_public_dns>                     # the trading UI
```

### Tear down

```bash
cd infra/terraform && terraform destroy
```

## Security notes for a real deployment

- `admin_cidr` should be your IP (`x.x.x.x/32`), never `0.0.0.0/0`.
- Put the backend/frontend behind HTTPS (ACM cert + ALB, or Caddy/Traefik on
  the host) before exposing it publicly; the WebSocket then upgrades to `wss`.
- Tighten CORS in `backend/main.py` from `*` to the frontend origin.
- Store `db_password` in AWS Secrets Manager or SSM rather than a tfvars file.