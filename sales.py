import time

# Create an empty dictionary to hold the customer totals
sales_by_customer = {}

start_time = time.time()

with open('output.txt', 'r') as f:
    for line in f:
        # Split the line into customer_id and sale_amount
        customer_id, sale_amount = line.split()
        # Convert them to the appropriate types
        customer_id = int(customer_id)
        sale_amount = int(sale_amount)
        # Add the sale_amount to the customer's total in the dictionary
        if customer_id not in sales_by_customer:
            sales_by_customer[customer_id] = 0
        sales_by_customer[customer_id] += sale_amount

end_time = time.time()

# Print the results
for customer_id, total_sales in sales_by_customer.items():
    print(f'Customer {customer_id} total sales: {total_sales}')

print(f'\nTime taken: {end_time - start_time} seconds')