import msal
import requests
import os
from datetime import datetime
import shutil

# --- Configuration ---
CLIENT_ID = 'your-client-id'
CLIENT_SECRET = 'your-client-secret'
TENANT_ID = 'your-tenant-id'
SITE_ID = 'your-sharepoint-site-id' # Format: tenant.sharepoint.com,site-id,web-id
DRIVE_ID = 'your-document-library-drive-id'

LOG_FILE = 'traffic_log.jsonl'
AUTHORITY = f'https://login.microsoftonline.com/{TENANT_ID}'
SCOPES = ['https://graph.microsoft.com/.default']

def get_access_token():
    app = msal.ConfidentialClientApplication(
        CLIENT_ID, authority=AUTHORITY, client_credential=CLIENT_SECRET
    )
    result = app.acquire_token_silent(SCOPES, account=None)
    if not result:
        result = app.acquire_token_for_client(scopes=SCOPES)
    
    if "access_token" in result:
        return result["access_token"]
    else:
        raise Exception(f"Failed to acquire token: {result.get('error_description')}")

def upload_to_sharepoint(token, file_path):
    # Rename file with timestamp to prevent overwriting in SharePoint
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    upload_name = f"traffic_log_{timestamp}.jsonl"
    
    headers = {
        'Authorization': f'Bearer {token}',
        'Content-Type': 'application/json'
    }
    
    # Graph API Endpoint for uploading small files (< 4MB) to a Drive
    endpoint = f"https://graph.microsoft.com/v1.0/sites/{SITE_ID}/drives/{DRIVE_ID}/root:/{upload_name}:/content"

    with open(file_path, 'rb') as f:
        file_data = f.read()
        
    response = requests.put(endpoint, headers=headers, data=file_data)
    
    if response.status_code in [200, 201]:
        print(f"Successfully uploaded {upload_name} to SharePoint.")
        return True
    else:
        print(f"Upload failed: {response.text}")
        return False

def main():
    if not os.path.exists(LOG_FILE):
        print("No log file found. Exiting.")
        return

    try:
        token = get_access_token()
        success = upload_to_sharepoint(token, LOG_FILE)
        
        if success:
            # Archive the local file so we start fresh for the next batch
            archive_name = f"archive_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jsonl"
            shutil.move(LOG_FILE, archive_name)
            print("Local log archived.")
            
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
