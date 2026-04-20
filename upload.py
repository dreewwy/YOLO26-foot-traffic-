import json
import msal
import requests
import os
from datetime import datetime
import shutil
from dotenv import load_dotenv

load_dotenv(dotenv_path='.env')

CLIENT_ID = os.getenv('CLIENT_ID')
CLIENT_SECRET = os.getenv('CLIENT_SECRET')
TENANT_ID = os.getenv('TENANT_ID')
SITE_ID = os.getenv('SITE_ID')
LIST_ID = os.getenv('LIST_ID')

LOG_FILE = 'traffic_log.jsonl'
AUTHORITY = f'https://login.microsoftonline.com/{TENANT_ID}'
SCOPES = ['https://graph.microsoft.com/.default']

def get_access_token():
    app = msal.ConfidentialClientApplication(CLIENT_ID, authority=AUTHORITY, client_credential=CLIENT_SECRET)
    result = app.acquire_token_silent(SCOPES, account=None)
    if not result:
        result = app.acquire_token_for_client(scopes=SCOPES)
    if "access_token" in result:
        return result["access_token"]
    raise Exception(f"Failed to acquire token: {result.get('error_description')}")

def process_daily_totals(file_path):
    total_in, total_out, peak_occupancy = 0, 0, 0
    
    with open(file_path, 'r') as f:
        for line in f:
            if not line.strip(): continue
            data = json.loads(line)
            
            # C++ arrays are cumulative, so the highest number is the daily total
            total_in = max(total_in, data.get('in', 0))
            total_out = max(total_out, data.get('out', 0))
            
            # Calculate how many people were inside at this exact moment
            current_occupancy = data.get('in', 0) - data.get('out', 0)
            peak_occupancy = max(peak_occupancy, current_occupancy)
            
    return total_in, total_out, peak_occupancy

def push_to_sharepoint_list(token, total_in, total_out, peak_occupancy):
    endpoint = f"https://graph.microsoft.com/v1.0/sites/{SITE_ID}/lists/{LIST_ID}/items"
    headers = {
        'Authorization': f'Bearer {token}',
        'Content-Type': 'application/json'
    }
    
    # Format today's date for the Title column
    today_str = datetime.now().strftime("%Y-%m-%d")
    
    payload = {
        "fields": {
            "Title": today_str,
            "TotalIn": total_in,
            "TotalOut": total_out,
            "PeakOccupancy": peak_occupancy
        }
    }
    
    response = requests.post(endpoint, headers=headers, json=payload)
    if response.status_code in [200, 201]:
        print(f"Successfully pushed summary to SharePoint: {today_str} | In: {total_in} | Peak: {peak_occupancy}")
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
        total_in, total_out, peak_occupancy = process_daily_totals(LOG_FILE)
        
        success = push_to_sharepoint_list(token, total_in, total_out, peak_occupancy)
        
        if success:
            archive_name = f"archive_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jsonl"
            shutil.move(LOG_FILE, archive_name)
            print("Local log archived. Ready for tomorrow.")
            
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
